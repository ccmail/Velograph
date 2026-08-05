# 01 Planner：analyzer 路径、谓词归一化与 graph 解析

> 服务里程碑：M1（graph 解析、朴素计划）、M2（谓词归一化落地）。
> 前置阅读：`00_overview.md`。

## 1. 现状与缺口（代码锚点）

当前唯一的生产执行路径（活路径）：

```text
InterpreterFactory.cpp:139-140
  GQLSingleQuery / GQLCombinedQuery → "InterpreterGQLQueryAnalyzer"
InterpreterGQLQueryAnalyzer（src/Interpreters/InterpreterGQLQueryAnalyzer.cpp）
  → buildGQLQueryTree + 通用 QueryTreePassManager（注册 GQLNameResolutionPass、GQLPredicateNormalizationPass）
  → GQLPlanner::buildQueryPlanIfNeeded
  → buildGQLQueryPlanFromTree（GQLPlanner.cpp:631）
  → planLinearQueryFromTree（GQLPlanner.cpp:493）
  → planMatchFromTree（GQLPlanner.cpp:386）
```

缺口清单（M1/M2 逐项消除）：

| # | 缺口 | 位置 |
|---|---|---|
| G1 | `planMatchFromTree` 写死 `nullptr` storage，`MatchStep` 落入发空行的 `MatchSource` | `GQLPlanner.cpp:393` |
| G2 | `buildMatchSpecFromTree` 有损：只拷贝变量与方向，丢标签/属性/内联谓词 | `GQLPlanner.cpp:340-375` |
| G3 | `buildActionsNode` 只支持 `ColumnNode`，无属性访问，`WHERE a.age > 20` 无法构建 | `GQLPlanner.cpp:407` |
| G4 | 无谓词归一化 pass；`GQLMatchNode::getWhere` 之外的谓词来源（内联 `WHERE`、属性 map、标签）没有进入 `FilterStep` 的通路 | `Analyzer/GQL/Passes/` 仅有 `GQLNameResolutionPass` |
| G5 | analyzer 路径没有 active graph 概念，无从解析 `GraphStorageEngine` | `GQLPlanner.cpp:390-392` 注释 |

旧 direct planner 路径（`InterpreterGQLQuery` → `SourcePlanner` → `MatchPlanner::planMatchClauseSequence`
→ `resolveGraphStorage`）为遗留冻结代码：**不修改、不接存储、不作为任何新代码的模仿对象**，
退役安排见 `06_milestones.md` 清理轨道。子查询回环（`SubqueryPlanner.cpp:117` 调回
direct 路径）在 M5 前不处理，touching 它的改动一律越界。

## 2. planner 的职责契约（P2 的展开）

`planMatchFromTree` 重构后的全部职责，按序：

1. 从 QueryTree 的 `GQLMatchNode` 构造 `MatchSpec`（结构信息：路径拓扑、变量名、
   方向；谓词类信息一律不进 spec——见第 4 节）。
2. 解析 `GraphStoragePtr`（见第 5 节）；解析失败抛异常，禁止回退 null。
3. `plan.addStep(MatchStep(spec, storage, referenced_columns, context))`，
   header 构造见 `02_property_columns.md` 第 4 节。
4. 若 `GQLMatchNode::getWhere() != nullptr`（经归一化 pass 后它承载完整合取谓词），
   调 `planFilter` 追加 `FilterStep`。

明确禁止 planner 做的事：谓词拆分、谓词绑定、下推判断、模式展开、
per-element 谓词进 spec。这些全部属于优化器（`03/04` 号文档）。

## 3. `MatchStep`（逻辑步）的最终形态

`src/Processors/QueryPlan/Graph/MatchStep.h` 演进为纯逻辑容器：

```cpp
class MatchStep final : public ISourceStep
{
public:
    MatchStep(MatchSpec spec_, GraphStoragePtr storage_, Names referenced_columns_, ContextPtr context_);

    const MatchSpec & getMatchSpec() const;
    const GraphStoragePtr & getStorage() const;
    const Names & getReferencedColumns() const;   /// 计划层列名，如 {"a", "a.age", "e.weight", "b.name"}

    /// 逻辑步必须在 expandMatchSteps 中被替换；执行到此说明展开 pass 未运行或未覆盖该形态。
    void initializePipeline(...) override;        /// throw LOGICAL_ERROR
};
```

要点：

- `initializePipeline` 抛 `LOGICAL_ERROR`（P3）：`MatchSource` 及"storage 非空时
  scan 首节点"的临时分支（`MatchStep.cpp:240-276`）整体删除。
- `MatchSpec` 保留既有结构字段；其中 `where_clause` 与 per-element 的
  `label_expression` / `properties` / `predicate` 字段在 analyzer 路径不再填写
  （谓词全部走 QueryTree 归一化），字段本身待 direct planner 退役后随清理轨道删除。
- header 仍由 step 自行构造（基于 `referenced_columns`），见 02 号文档。

## 4. 谓词归一化 pass（`GQLPredicateNormalizationPass`，M2）

新增 `src/Analyzer/GQL/Passes/GQLPredicateNormalizationPass.{h,cpp}`，通过
`GQL::addQueryTreePasses` 注册到通用 `QueryTreePassManager`，**位于
`GQLNameResolutionPass` 之后**（依赖名字解析产出的 `ColumnNode`）。

对每个 `GQLMatchNode`，将四类谓词来源改写为一个合取，写回 `getWhere()`：

| 来源 | 语法示例 | 改写结果（QueryTree 表达式） |
|---|---|---|
| 顶层 `WHERE` | `MATCH ... WHERE a.age > 20` | 原样保留为 conjunct |
| 内联元素 `WHERE` | `(a WHERE a.age > 20)` | 提升为 conjunct，从 pattern 节点上摘除 |
| 属性 map | `(a {age: 20, city: 'SH'})` | 每个键值对展开为 `a.age = 20 AND a.city = 'SH'` |
| 标签/类型 | `(a:Person)`、`[e:KNOWS]` | 点：`has(a.labels, 'Person')`；边：`e.type = 'KNOWS'`（D9） |

规则：

- 合取用二元 `and` 函数节点级联；单 conjunct 不包 `and`。
- 标签表达式的析取/否定（`:A|B`、`:!A`）对应改写为 `or` / `not` 组合；
  遇到尚不支持的标签表达式形态抛 `NOT_IMPLEMENTED`（P3），不允许静默丢弃。
- pass 运行后，`GQLNodePatternNode` / `GQLEdgePatternNode` 上不得残留任何谓词类
  子节点；`buildMatchSpecFromTree` 断言这一点（防御 G2 类回归）。
- 属性访问的表达形式（`a.age` 如何成为 `ColumnNode`）见 `02_property_columns.md`
  第 3 节，该文档同时约定本 pass 与名字解析 pass 的协作细节。

这样设计的直接收益（P1）：`planFilter` 之后计划里只有标准 `FilterStep`，
优化器不需要知道谓词的语法出身，北极星查询的两个 conjunct 与用户手写
`MATCH (a)-[e]->(b) {weight: 0.5}` 风格变体走完全相同的下推机制。

## 5. graph 解析（M1 核心）

新增 helper（放 `src/Interpreters/GQL/GraphResolver.{h,cpp}`）：

```cpp
/// 从 context 解析当前会话/查询绑定的图，返回 GraphStorageEngine。
/// 解析顺序：
///   1. MatchSpec.graph_reference（USE g / SELECT FROM g MATCH，如已绑定）
///   2. context 当前数据库：若该数据库注册了 GraphStorageEngine（StorageID{db, "_graph"}），用之
/// 全部失败 → 抛 UNKNOWN_TABLE（"no active graph for MATCH"），不回退 null。
GraphStoragePtr resolveActiveGraphStorage(const Graph::MatchSpec & spec, ContextPtr context);
```

依据：`GraphStorageEngine` 构造时以 `StorageID{graph_name, "_graph"}` 注册进
`DatabaseCatalog`（`GraphStorageEngine.h:50-53`），因此 M1 的最小解析是
`DatabaseCatalog::tryGetTable({current_database, "_graph"})` + downcast 到
`IGraphStorage`。`USE graph` 的 scope 传递在 analyzer 路径补全之前，M1 验收
允许仅支持"当前数据库即图"的形态，但 `graph_reference` 非空而无法解析时必须抛异常。

## 6. 验收要点

- M1：`MATCH (n) RETURN n` 经展开 pass 出真实数据（详见 06 号文档 M1 节）。
- M2：北极星查询在**关闭下推**（或下推尚未实现）时结果正确——
  全扫描 + `FilterStep`，谓词归一化对四类来源等价生效。
- 回归：`OPTIONAL MATCH`、match mode、量词、路径别名等未实现形态全部
  `NOT_IMPLEMENTED`，与现状一致（`GQLPlanner.cpp:353-360` 的 fail-closed 保持）。
