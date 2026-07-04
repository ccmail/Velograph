# 05 存储原语：签名演进与 MergeTree 扫描适配

> 服务里程碑：M3（scan/getVertex + 谓词）、M4（getNeighbors + 运行时 id 集合）。
> 前置阅读：`00_overview.md`。
> 本文回答"存储侧如何在扫描时就把无用数据滤掉"：`IGraphStorage` 原语接受
> 谓词与投影，`GraphStorageEngine` 内部把它们装配成 `SelectQueryInfo`，
> 一次 `MergeTreeDataSelectExecutor::read` 内完成 granule 跳读（`KeyCondition`）、
> 行级过滤（prewhere）与列裁剪。这正是 ClickHouse `IStorage::read(query_info)`
> 模式在图原语上的复刻。

## 1. 签名演进（M3 落地）

三段式分层（NameSet → header+mask → `xxxImpl`）保持不变；变化有两处：

1. **四个原语全部新增谓词参数** `const ActionsDAG * filter`（可空），
   三个 stage 全程透传。DAG 的输入列一律是**存储层列名**（P6：`age`、`__ID__`、
   `type`……），计划层名的翻译在物理算子完成，原语不认识 `a.age`。
2. **废除 `getNeighbors` 的 `ASTPtr filter_pushdown` 参数**（D4）。
   `limit_per_vertex` 保留（0 = 不限，M4 不实现，透传备用）。

演进后 Stage-1 签名（Stage-2/Impl 同步加 `filter`，不再赘列）：

```cpp
Pipe scan        (const NameSet & column_names, GraphElementKind kind,
                  const ActionsDAG * filter,
                  size_t max_block_size, size_t num_streams);

Pipe getVertex   (const NameSet & column_names, const Columns & id_columns,
                  const ActionsDAG * filter,
                  size_t max_block_size, size_t num_streams);

Pipe getEdge     (const NameSet & column_names, const Columns & edge_key_columns,
                  const ActionsDAG * filter,
                  size_t max_block_size, size_t num_streams);

Pipe getNeighbors(const NameSet & column_names, GraphDirection direction,
                  const Columns & input_vertex_columns,
                  const ActionsDAG * filter,
                  size_t max_block_size, size_t num_streams,
                  size_t limit_per_vertex);
```

调用方约定（03 号文档）：驱动型算子只传原始 id 列（`id_columns` /
`input_vertex_columns`），**不自行构造 IN 谓词**——id 集合条件由引擎内部
生成并与 `filter` 合取，保证 IN-set 的构造方式（下节）对调用方透明。

## 2. `SelectQueryInfo` 装配（`GraphStorageEngine` 内部，M3 核心）

新增私有 helper，替换现 `readFromInternalTable`（`GraphStorageEngine.cpp:80`）
中"空 `SelectQueryInfo`"的占位逻辑：

```cpp
/// 把（可空的）谓词 DAG 与（可空的）运行时 id 集合装配进 SelectQueryInfo，
/// 使随后的 read() 获得 KeyCondition 裁剪 + prewhere 行过滤。
SelectQueryInfo buildReadQueryInfo(
    const ActionsDAG * filter,        /// 存储层列名；可为 null
    const String & key_column,        /// 生成 `key_column IN ids`；空串 = 无 id 条件
    const Columns & id_columns,       /// 运行时 id；可为空
    const Block & table_header,
    ContextPtr context) const;
```

装配步骤：

1. **id 集合条件**（`key_column` 非空且 `id_columns` 非空时）：
   - id 列去重后构造 `FutureSetFromTuple`（public 构造，`PreparedSets.h:92-94`）；
   - 包成 `ColumnSet`（set 已就绪时再包 `ColumnConst`），作为常量列加入 DAG，
     构造函数节点 `in(<key_column>, <set 常量>)`。
     **仓内既有模式**：`PlannerActionsVisitor.cpp:857-868`（SQL 侧 IN 的
     标准构造），照抄即可；set 内嵌于 DAG，`KeyCondition` 直接识别
     `ColumnSet` 常量，**无需触碰 `query_info.prepared_sets`**。
2. **合取**：`final = filter AND in(...)`（一方为空则取另一方；均空则返回
   默认 `query_info`，行为退化为现状的全扫描 + 投影）。
3. **注入两个通道**（同一 DAG，两份用途）：
   - `query_info.filter_actions_dag = final 的 shared 克隆`
     （`SelectQueryInfo.h:167`）→ `read()` 内部据此构建 `KeyCondition`，
     对主键列做 mark 级二分/集合跳读；
   - `query_info.prewhere_info = std::make_shared<PrewhereInfo>(final 克隆,
     结果列名)`，`need_filter = true`、`remove_prewhere_column = true`
     （`SelectQueryInfo.h:42-55`）→ granule 读出后、返回前行级过滤。
     prewhere 引用而调用方未投影的列由 MergeTree 读取端自动补齐并在
     输出前裁掉，无需调用方关心。
4. `query_info.is_internal = true` 保持现状。

读取入口维持 `MergeTreeDataSelectExecutor::read`（D7；
`MergeTreeDataSelectExecutor.h:33`），现有 `plan → buildQueryPipeline → getPipe`
封装（`GraphStorageEngine.cpp:107-115`）不变。

## 3. 各原语的 `KeyCondition` 预期收益

| 原语 | 引擎生成的 id 条件 | 表 / 排序键 | 收益 |
|---|---|---|---|
| `scan(Vertex)` | 无 | `vertices` / `__ID__` | 仅 caller 谓词若含 `__ID__` 比较则跳 granule；否则全扫 + prewhere |
| `getVertex` | `__ID__ IN ids` | `vertices` / `__ID__` | 精确二分到目标 granule |
| `getEdge` | 复合键 `(__SRC__, type, __RANK__)` 前缀（按需） | `edges_forward` | 前缀匹配（当前无算子消费，签名先统一，实现留空到有消费方为止；注意 schema 无独立边 id 列） |
| `getNeighbors(FORWARD)` | `__SRC__ IN ids` | `edges_forward` / `(__SRC__, type, __RANK__)` | 跳到源点邻接段；caller 谓词含 `type = ...` 时命中第二键列（D9） |
| `getNeighbors(BACKWARD)` | `__DST__ IN ids` | `edges_reverse` / `(__DST__, type, __RANK__)` | 同上 |

`MergeTree` 的 `ORDER BY` 即图邻接索引：同一 `__SRC__` 的边物理连续，
`IN` 集合条件经 `markRangesFromPKRange` 二分定位，等价于 KV 图库的
prefix scan（背景论证见飞书设计文档第 2/3/6 章，此处不重复）。

## 4. 运行时 id 集合的规模约束

驱动型算子按输入 chunk 批量调用原语（03 号文档第 4 节），id 集合规模
天然被 chunk 行数（默认 65409）封顶，无需额外切分逻辑。超大 IN 集合的
`KeyCondition` 退化（relaxed 标记、排除搜索）在 chunk 粒度下不构成风险，
M5 之前不做专项处理。

## 5. 存量缺陷修复（并入里程碑）

| 缺陷 | 现状 | 处置 | 里程碑 |
|---|---|---|---|
| `getNeighborsImpl` 的 `switch` 无兜底返回 | `GraphStorageEngine.cpp:183-206`，GCC 报 warning 且非法枚举值 UB | 末尾补 `default: throw LOGICAL_ERROR` | M1 |
| 存储层 `UNDIRECTED` 两表 union 后无统一 join key | `GraphStorageEngine.cpp:191-205` | **删除该分支**，改抛 `NOT_IMPLEMENTED`（无向语义由算子层以 FORWARD+BACKWARD 两次调用实现，03 号文档第 4 节）；`IGraphStorage.h` 的 `UNDIRECTED` 注释同步改写 | M4 |
| `edges_reverse` 物理布局未经读路径验证 | `writeEdge` 声称写反向表时交换 `__SRC__`/`__DST__`（`GraphStorageEngine.h:121-124`） | gtest：写入若干边后分别以 FORWARD/BACKWARD 读取，断言邻居 id 与属性对齐 | M4 |
| 四个 `xxxImpl` 全扫描 TODO | `GraphStorageEngine.cpp:120-207` | 被本文签名演进与装配逻辑整体取代 | M3/M4 |

## 6. 验收要点

- 单元测试直接构造引擎实例测原语（延续 `registerVertexType` / `writeVertex` /
  `writeEdge` 的 gtest 用法），不经过 GQL 端到端链路：
  - `getVertex({5,7})` 只返回两行；`ProfileEvents::SelectedMarks` 小于全表扫描值；
  - `scan(Vertex, filter: age>20)` 返回行数与 prewhere 语义一致；
  - `getNeighbors(FORWARD, {v1}, filter: weight>0.5)` 只返回满足谓词的邻接边；
  - BACKWARD 读取结果为源视角（基类 swap 生效，`IGraphStorage.cpp:25-27`）。
- 谓词含未投影列（filter 引用 `age`，投影只要 `name`）：结果正确且输出
  header 不含 `age`（第 2.3 节自动补列-裁列行为）。
