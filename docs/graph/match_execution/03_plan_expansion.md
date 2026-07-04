# 03 计划展开：`expandMatchSteps` 与物理算子契约

> 服务里程碑：M1（最小展开）、M4（expand 链）。前置阅读：`00_overview.md`、`02_property_columns.md`。
> 本文落实 D2/D3：逻辑 `MatchStep` 在优化开始前展开为物理子算子树，
> **参与后续优化 pass 的是子算子**。

## 1. `expandMatchSteps` pass

### 1.1 位置与机制

- 新增 `src/Processors/QueryPlan/Optimizations/expandMatchSteps.{h,cpp}`（函数放
  `DB::QueryPlanOptimizations` 命名空间）。
- 调用点：`optimizeTree.cpp` 的 `optimizeTreeFirstPass` 入口处，且必须放在
  `if (!optimization_settings.optimize_plan) return;`（`optimizeTree.cpp:44`）
  **之前**——展开是 lowering 而不是优化，关闭计划优化
  （`query_plan_enable_optimizations = 0`）时也必须执行，此时谓词全部留在
  `FilterStep` 朴素求值，结果仍正确（这也是 D8 两态对照的基础）。展开后才轮到
  现有 first-pass 优化（`tryPushDownFilter` 等看到的已是展开树）。无条件全树
  遍历：非 GQL 计划里不存在 `MatchStep`，遍历零命中即零开销，不加开关。
- 节点替换机制参考"逻辑步转物理步"的既有先例 `convertLogicalJoinToPhysical`
  （`Optimizations.h:195`，于二阶段调用；本 pass 只借鉴其节点替换写法）：遍历
  `QueryPlan::Node`，命中 `typeid_cast<Graph::MatchStep *>` 的节点，就地把该节点
  替换为子算子链的根，新增节点挂入 `QueryPlan::Nodes`（旧节点无需摘除，
  遵循 `Optimizations.h:28-29` 的约定）。
- 收益顺带说明：`EXPLAIN PLAN optimize=0` 展示逻辑 `MatchStep`，
  `optimize=1` 展示物理算子树，调试时两层形态都可见。

### 1.2 展开规则（M4 完整形态）

输入：`MatchStep{spec, storage, referenced_columns}`。约束检查（违反抛
`NOT_IMPLEMENTED`，P3）：单 `MatchClauseSpec`、单 path、无 alternation /
quantifier / prefix / optional / match-mode / yield，边方向属于
`{Outgoing, Incoming, Undirected}`（复合方向 M5）。

对线性模式 `(v0)-[e1]-(v1)-[e2]-(v2)-...-[ek]-(vk)`，产出左深链（锚点 = 最左点
`v0`，D10）：

```text
MatchVertexStep(v0)                          ← 叶子源
  → MatchExpandStep(e1: v0→v1)               ← 每条边一个
  → MatchVertexLookupStep(v1)                 ← 仅当 v1 有被引用属性列
  → MatchExpandStep(e2: v1→v2)
  → MatchVertexLookupStep(v2)
  → ...
```

特例：

- 单点模式 `(n)` → 仅 `MatchVertexStep(n)`（M1 即此形态）。
- 纯边模式 `()-[e]->()`（两端匿名且无属性引用）→ 仅 `MatchEdgeStep(e)`。
- 匿名元素（无变量名）不产生 lookup，也不进 header；匿名中间点仍产生 expand
  （用内部合成列名 `__anon_<i>` 作链接 id，不外露）。
- 同一 `MATCH` 多 path、多 `MATCH` 子句（`spec.clauses.size() > 1`）→ M5
  （需要 `MatchJoinStep`），当前 `NOT_IMPLEMENTED`。

## 2. 物理算子总则

- 文件位置：`src/Processors/QueryPlan/Graph/` 下每算子一对 `.h/.cpp`。
- 全部持有 `GraphStoragePtr storage` 与 `ContextPtr`，输出 header 全部用
  **计划层列名**（02 号文档），与存储层名的互译封在各自 `initializePipeline`。
- 全部实现 `clone`、`describeActions`（`EXPLAIN` 可读：变量、方向、已绑定谓词）。
- 谓词槽位统一（04 号文档给绑定规则）：

```cpp
/// 各图物理算子共有的谓词槽（叶子源直接继承 SourceStepWithFilterBase 获得；
/// 驱动型算子自持同构成员）。绑定的 DAG 输入列均为计划层列名。
std::shared_ptr<const ActionsDAG> bound_filter;   /// 可为空
```

## 3. 叶子源：`MatchVertexStep` / `MatchEdgeStep`

```cpp
class MatchVertexStep final : public SourceStepWithFilterBase
{
public:
    MatchVertexStep(GraphStoragePtr storage_, String variable_,
                    Names referenced_columns_ /*本变量的计划层列*/, ContextPtr context_);
    String getName() const override { return "MatchVertex"; }
    /// applyFilters 无需 override：基类默认实现（SourceStepWithFilter.cpp:62-65）
    /// 已把收到的 filter nodes 经 buildFilterActionsDAG 合并进 filter_actions_dag。
    void initializePipeline(...) override;
};
```

- **继承 `SourceStepWithFilterBase` 是本设计的支点**：现有二阶段 pass
  `optimizePrimaryKeyConditionAndLimit`（`optimizePrimaryKeyConditionAndLimit.cpp:12`
  对该基类 `dynamic_cast` 泛化生效）会自动把上方 `FilterStep` 的 DAG 通过
  `addFilter` 送进来，**零新增优化器代码**拿到谓词。
- `initializePipeline`：
  1. `detachFilterActionsDAG()` 取合并谓词（计划层名）；
  2. 按 02 号文档第 5 节翻译列名 → 存储层投影 `NameSet` + 存储层谓词 DAG；
  3. 交 `IGraphStorage::scan`（05 号文档新签名，携带谓词）拿 `Pipe`；
  4. 输出列重命名回计划层名，`pipeline.init`。
- `MatchEdgeStep` 同构，`kind = Edge`，读 forward 表。

## 4. 驱动型算子：`MatchExpandStep` / `MatchVertexLookupStep`

```cpp
class MatchExpandStep final : public ITransformingStep
{
public:
    MatchExpandStep(GraphStoragePtr storage_, String source_variable_, String edge_variable_,
                    String target_variable_, GraphDirection direction_,
                    Names referenced_edge_columns_, ContextPtr context_);
    String getName() const override { return "MatchExpand"; }
    void bindFilter(ActionsDAG dag, String filter_column);   /// 供 04 号文档的图 pass 调用
    void transformPipeline(...) override;
};
```

- **header 公式**：`输出 = 输入全列 + e 的被引用列 + 目标 id 列 target`
  （target 列来自边行的邻居端 id 重命名，无需回表）。
  `MatchVertexLookupStep` 的公式：`输出 = 输入全列 + v 的被引用属性列`。
- **执行模型（M4 定版：块驱动批量 lookup）**：`transformPipeline` 挂自定义
  transform processor，对每个输入 chunk：
  1. 抽取驱动 id 列（expand 取 `source_variable` 列；lookup 取 `variable` 列），
     块内去重排序；
  2. 以该 id 集合调存储原语（expand → `getNeighbors`；lookup → `getVertex`），
     运行时 id 集合注入机制见 05 号文档第 4 节；绑定谓词随调用下推；
  3. 用 `PullingPipelineExecutor` 同步拉完该批结果，按 id 建哈希表；
  4. 输入行探测哈希表，逐匹配展开输出（inner 语义：无匹配的输入行丢弃；
     一对多时输入行复制多份）。
- 并行度：M4 驱动型算子单流（上游 `resize(1)`），扫描叶子源保持多流。
  流式/异步/多流 expand 是 M5 优化项，接口不为其预留复杂度。
- **方向语义**：`Outgoing → GraphDirection::FORWARD`，`Incoming → BACKWARD`
  （基类已做 src/dst 换位，输出恒为"源视角"）。
  `Undirected` **不使用存储层 `UNDIRECTED` 枚举**，而是在算子内做
  `FORWARD` + `BACKWARD` 两次原语调用取并（两者皆源视角，列布局一致，
  直接 union）；这从算子层面消解了存储 `UNDIRECTED` 两表 union 后
  join key 不一致的缺陷（05 号文档第 6 节配套清理）。自环边会出现两次，
  记为已知偏差，M5 处理。

## 5. id 列与链接不变式

- 展开链上每个算子的输出必须包含**其后所有算子需要的驱动 id 列**——
  02 号文档第 4 节因此规定被引用列收集时强制加入各变量 id 列。
- 匿名中间点的合成 id 列 `__anon_<i>` 在链尾（最后一个需要它的算子之后）由
  紧随的 `ExpressionStep`（展开 pass 生成）投影掉，不进入 `MatchStep`
  对外承诺的 header。
- 展开 pass 完成后必须断言：链尾输出 header 与原 `MatchStep::getOutputHeader`
  完全一致（列名、类型、顺序），保证替换对上层计划透明。

## 6. `MatchJoinStep`（M5 预留）

多 path、环闭合（`(a)-[..]->(b), (a)-[..]->(c), (b)-[..]->(c)`）、多 `MATCH`
组合需要 join 型算子。命名预留 `MatchJoinStep`；候选实现是直接复用
`JoinStepLogical`（换取 `tryPushDownFilter` / join 优化的全部既有支持）。
M5 开工前出补充设计（含 expand-vs-join 的代价择优、锚点选择），本轮不实现、
不预埋接口。

## 7. 验收要点

- M1：`MATCH (n) RETURN n` 展开为单 `MatchVertexStep`，`EXPLAIN PLAN` 可见。
- M4：北极星查询展开为
  `MatchVertexStep(a) → MatchExpandStep(e) → MatchVertexLookupStep(b)` 链，
  端到端结果与朴素全扫描 + `FilterStep` 基线一致（谓词下推开关两态对比）。
- 不变式断言（第 5 节）纳入 gtest：对代表性模式断言展开前后 header 一致。
