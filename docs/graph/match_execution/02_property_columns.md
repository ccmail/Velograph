# 02 属性列模型

> 服务里程碑：M2。前置阅读：`00_overview.md`。
> 本文回答一个问题：`WHERE a.age > 20` 里的 `a.age`，作为一列数据，
> 从哪里来、叫什么名字、什么类型、谁负责把它变出来。

## 1. 问题定义

`FilterStep` / `ExpressionStep` 只会对 **header 里已存在的列** 求值。今天
`MatchStep::makeHeader`（`MatchStep.cpp:204`）给每个模式变量造一个 `UInt64` 列
（`a`、`e`、`b`），header 里根本没有 `a.age` 这一列，所以：

- `RETURN a.name`、`WHERE a.age > 20` 在活路径上无法构建计划（G3）；
- 就算能构建，存储层也不知道要读 `age` 这个属性列。

属性列模型 = 一套让"变量的属性"成为"计划里的普通列"的端到端约定。
选型为 **eager 投影**（D6）：查询引用了哪些属性，`MatchStep`（及展开后的
物理算子）的输出 header 就直接携带哪些属性列，由存储扫描时顺带读出。
不引用的属性永远不读（列裁剪由存储三段式投影完成）。

备选的 lazy 方案（header 只留 id，属性用到时再回表 lookup）在超级节点场景
省内存，但需要额外的 lookup 算子和延迟物化机制，作为 M5 之后的优化方向，
本轮不做。

## 2. 命名约定（唯一权威）

| 列 | 计划层名字 | 存储层名字 | 类型 |
|---|---|---|---|
| 变量 `a` 的 id | `a` | `__ID__`（vertices 表） | `UInt64` |
| 变量 `a` 的属性 `age` | `a.age` | `age`（vertices 表） | 属性注册类型 |
| 变量 `a` 的标签 | `a.labels` | `labels` | `Array(String)`（`GraphSchemaRegistry.cpp:21`） |
| 边 `e` 的源/目标 | `e.__SRC__` / `e.__DST__` | `__SRC__` / `__DST__` | `UInt64` |
| 边 `e` 的类型 | `e.type` | `type` | `String` |
| 边 `e` 的属性 `weight` | `e.weight` | `weight`（edges 表） | 属性注册类型 |

规则：

- 计划层列名 = `<变量名>` （id 列）或 `<变量名>.<属性名>`（属性列）。
  分隔符就是字面 `.`；`Block` 列名是普通字符串，不存在解析歧义。
- **翻译边界（P6）**：`计划层名 ↔ 存储层名` 的互译只发生在物理算子
  （`MatchVertexStep` 等）内部——算子知道自己绑定哪个变量，剥掉前缀即得存储列名。
  `IGraphStorage` 原语的入参（投影 `NameSet`、谓词 DAG 的输入列）一律是存储层名。
- 变量裸引用（`RETURN a`）在 M2 阶段语义 = id 列 `a`（`UInt64`）。GQL 标准的
  "变量代表元素整体"（返回点/边的全部属性构成的记录）需要专门的值类型，非目标。
- `id(v)` 函数调用解析为 id 列引用（`ColumnNode{name: "v"}`），是上一条的自然
  延伸，属名字解析 pass 范围；`WHERE id(a) IN (1, 2, 3)` 因此就是对列 `a` 的
  IN 谓词。

## 3. Analyzer 侧：属性访问如何解析（M2 新增）

现状：`GQLNameResolutionPass` 只把裸标识符 `a` 解析成指向 `GQLMatchNode` 的
`ColumnNode{name: "a", UInt64}`；属性访问在 QueryTree 里没有表示（G3/G4）。

M2 扩展 `GQLNameResolutionPass`（或在其后新增子 pass，实现者按代码量自定，
不构成架构决策）：

1. **识别属性访问表达式**：QueryTree builder 把 `a.age` 构建成什么节点，
   以 `GQLQueryTreeBuilder.cpp` 现状为准（若 builder 尚未支持属性访问表达式，
   M2 需先在 builder 补上，产出形如 `property_access(base_identifier, name)` 的
   中间节点）。
2. **解析规则**：`base` 必须解析到可见的模式变量绑定；`name` 必须存在于该变量
   对应元素类型的 schema 属性集合（M2 阶段 schema 校验降级为"存在于内部表 header"，
   通过 `IGraphStorage::getGraphHeader` 查询；不存在 → 抛 `UNKNOWN_IDENTIFIER`）。
3. **解析产物**：`ColumnNode{name: "a.age", type: <属性类型>, source: GQLMatchNode}`。
   即：属性访问在解析后就是一个普通列引用，`buildActionsNode`（`GQLPlanner.cpp:407`）
   的 `ColumnNode` 分支无需感知属性概念，天然支持。

类型来源：`GraphStorageEngine::getGraphHeader(kind)` 返回的内部表 header
（`GraphStorageEngine.cpp:getGraphHeader`）。analyzer 需要拿到 storage 才能查类型，
因此 **graph 解析（01 号文档第 5 节）必须发生在名字解析之前或之中**——
实现上把 `GraphStoragePtr` 挂进 pass 可见的 context（`GQLQueryOptions` 或
pass manager 状态，实现者选，不构成架构决策）。

## 4. 被引用列收集与 header 构造（M2 修改 `makeHeader`）

1. analyzer 阶段结束后，遍历该 `GQLMatchNode` 作用域内全部表达式
  （`RETURN`、归一化后的 `WHERE`、后续 `ORDER BY` 等），收集
  `source == 该 MatchNode` 的 `ColumnNode` 名字集合，得到 `referenced_columns`
  （示例：北极星查询 → `{a, a.age, a.name, e.weight, b.name}`；
  注意 `a`/`b` 的 id 列即使未被显式引用，也**强制加入**——展开算子链接
  需要 id 列作 join key，见 03 号文档第 5 节）。
2. `referenced_columns` 存入 `MatchStep`，并构造输出 header：
   每列 `{name: 计划层名, type: 查 getGraphHeader}`，列序 = 变量在模式中
   出现序，同变量内 id 列在前、属性按名字典序（确定性，供测试断言）。
3. 展开后各物理算子的 header 是它的切片/级联（03 号文档给出每算子公式）。

## 5. 存储三段式投影的衔接

`IGraphStorage` 的 Stage-1 重载收 `NameSet column_names`（存储层名），
基类 `getReturnHeaderForColumns` 造列 mask（`IGraphStorage.cpp:93`）。
物理算子调用原语前：

```text
算子内翻译：referenced_columns 中属于本算子变量的列
  → 剥前缀 → 存储层 NameSet
  → 原语 Stage-1 → 存储读取只含这些列
  → 返回 Pipe 后，算子把输出 Block 列重命名回计划层名（AddingRenames / ExpressionStep actions）
```

重命名用 `ActionsDAG::makeConvertingActions` 或手工 alias DAG，实现者选。
方向性注意：`BACKWARD` 读反向表时基类已做 src/dst 语义交换
（`IGraphStorage.cpp:25-27`），算子层的翻译不要重复交换（05 号文档第 6 节）。

## 6. 验收要点（并入 M2）

- `MATCH (a) RETURN a.name` / `WHERE a.age > 20 RETURN a.name, a.age`：
  计划可构建，header 列名符合第 2 节约定，结果正确。
- 引用不存在属性：`UNKNOWN_IDENTIFIER`。
- `EXPLAIN header=1` 展示的各步 header 与本文约定一致。
- 存储侧验证列裁剪：只引用 `a.name` 时，`vertices` 表的其他属性列不读
  （可用 `ProfileEvents` 的读列数或 trace 日志断言）。
