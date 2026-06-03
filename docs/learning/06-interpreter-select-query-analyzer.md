# InterpreterSelectQueryAnalyzer 设计详解

> 本文档分析 `InterpreterSelectQueryAnalyzer` 的作用与在 Interpreter 阶段所做的工作。
> 源码: `src/Interpreters/InterpreterSelectQueryAnalyzer.h` / `.cpp`，与 `src/Planner/Planner.h`。

## 一、概述

`InterpreterSelectQueryAnalyzer` 是 ClickHouse "新分析器（new analyzer）" 路径下处理 `SELECT` 查询的解释器，
对应旧路径的 `InterpreterSelectQuery`。它的核心职责是把 `SELECT` 的 AST 经过
**QueryTree 构建 + 语义分析（passes）+ 计划构建** 之后，
产出一个可执行的 `QueryPlan` / `QueryPipelineBuilder` / `BlockIO`。

在整体查询流程中的位置：

```
SQL → Parser → AST
              ↓
   InterpreterSelectQueryAnalyzer            ← 本文档分析对象
   ├─ buildQueryTree            (AST → QueryTree)
   ├─ QueryTreePassManager      (resolve / 优化 passes)
   └─ Planner                   (QueryTree → QueryPlan)
              ↓
        QueryPlan → QueryPipeline → Executors
```

与 `InterpreterSelectQuery` 不同的是，新解释器并不直接在自身里实现
`executeWhere / executeAggregation / executeOrder` 等阶段，
而是把这些工作下放给 `Planner`。`InterpreterSelectQueryAnalyzer` 更像是一个
**协调器（orchestrator）**，负责把 "AST → QueryTree → 经过 passes 的 QueryTree" 这条上游链路串起来，
然后委托给 `planner` 完成下游的 `QueryPlan` 构建。

## 二、类的核心成员

```cpp
class InterpreterSelectQueryAnalyzer : public IInterpreter {
 private:
  ASTPtr query;                                  // 规范化后的 AST
  ContextMutablePtr context;                     // 拷贝并补充过的查询上下文
  SelectQueryOptions select_query_options;       // 查询选项（only_analyze、to_stage 等）
  QueryTreeNodePtr query_tree;                   // 经过 passes 的 QueryTree
  Planner planner;                               // 真正的计划构建器
  std::function<std::unique_ptr<QueryPlan>()>
      query_plan_with_parallel_replicas_builder; // 并行副本备选计划的延迟构建器
};
```

注意一个关键设计点：**`planner` 是一个直接持有的成员**（不是指针），
说明 analyzer 与 planner 在生命周期上 1:1 绑定，
"analyze 出来的 query tree" 必须由一个固定的 planner 接管。

## 三、三种构造路径（数据来源策略）

构造函数对应三种调用场景：

| 构造方式 | 适用场景 | 说明 |
|---------|---------|------|
| `(ASTPtr, ctx, options, column_names, post_filter)` | 普通 `SELECT`、视图查询 | 从 AST 起步，可携带列裁剪和外部下推过滤 |
| `(ASTPtr, ctx, options, storage, column_names)` | 已知具体存储的查询 | 构建 QueryTree 后用 `replaceStorageInQueryTree` 替换最左表节点 |
| `(QueryTreeNodePtr, ctx, options)` | 已经有 QueryTree 的内部调用 | **不再跑 passes**，仅做 planner 接入 |

所有构造路径都做这些事：

1. **`normalizeAndValidateQuery`**：把 `ASTSelectQuery` / `ASTSelectWithUnionQuery` / `ASTSubquery`
   统一成可用的查询 AST；如果调用方给了 `column_names`（典型场景：`StorageView`），
   外层会包裹一层 `SELECT col1, col2 FROM (<原查询>)`，从而支持只读取需要的列。
2. **`buildContext`**：基于传入 context 做 `Context::createCopy`，
   并在分片场景下注入 `_shard_num` / `_shard_count` 这类特殊标量。
3. **`buildQueryTreeAndRunPasses`**：从 AST 构造 `QueryTree`，并跑 `QueryTreePassManager`。
4. **构造 `planner`**：把 `query_tree`、`select_query_options`、可选 `post_filter` 交给 `Planner`。
5. **构造 `query_plan_with_parallel_replicas_builder`**：用闭包延迟保存
   "重新跑一遍 analyzer 以获得并行副本计划" 所需的最小输入（克隆的 AST + 拷贝的 context + options）。

## 四、Interpreter 阶段实际做的工作

可以把它的工作切成 4 个清晰的阶段：

### 4.1 AST 规范化（normalize）

```cpp
ASTPtr normalizeAndValidateQuery(const ASTPtr &query, const Names &column_names);
```

- 接受 `ASTSelectQuery`、`ASTSelectWithUnionQuery` 或 `ASTSubquery`，否则抛 `UNSUPPORTED_METHOD`。
- 若传入了 `column_names`，把原查询包成子查询，外层补一个只投影指定列的
  `SELECT col1, col2, ... FROM (<原查询>)`。这是支撑 `StorageView` /
  "view references" 列裁剪的关键，注释里也写明了这是为视图设计的。

### 4.2 QueryTree 构建 + Passes 运行（analyze）

```cpp
static QueryTreeNodePtr buildQueryTreeAndRunPasses(
    const ASTPtr &query,
    const SelectQueryOptions &select_query_options,
    const ContextPtr &context,
    const StoragePtr &storage)
{
  auto query_tree = buildQueryTree(query, context);

  QueryTreePassManager query_tree_pass_manager(context);
  addQueryTreePasses(query_tree_pass_manager, select_query_options.only_analyze);

  if (select_query_options.ignore_ast_optimizations ||
      select_query_options.is_create_view ||
      context->getClientInfo().query_kind == ClientInfo::QueryKind::SECONDARY_QUERY)
    query_tree_pass_manager.runOnlyResolve(query_tree);
  else
    query_tree_pass_manager.run(query_tree);

  if (storage)
    replaceStorageInQueryTree(query_tree, context, storage);

  return query_tree;
}
```

这是 analyzer "解释器" 名字真正生效的地方，干的活有：

- `buildQueryTree`：把 AST 翻译为 `QueryTree`（强类型的语义树，节点是
  `QueryNode` / `TableNode` / `IdentifierNode` / `TableFunctionNode` 等）。
- 通过 `QueryTreePassManager` 跑一套 **passes**，做：
  - **标识符与作用域解析（resolve）**：列名、别名、表名、子查询绑定；
  - **类型推导**与函数解析；
  - 各种 **查询树级优化**（在不被禁用的场景下）。
- 三种"裁剪"模式按场景生效：
  - **`only_analyze`**：传给 `addQueryTreePasses`，只跑分析所需的 passes（例如 `getSampleBlock` 只想拿 header）。
  - **`runOnlyResolve`**：分片二级查询、`CREATE VIEW`、显式 `ignore_ast_optimizations`
    等场景，只做名字解析，**不做树级优化**——因为这些场景下做优化可能改变 header，
    在分片节点上会引发不一致。
  - 默认：跑全部 passes。
- 如果调用方给了 `storage`（例如内部接入某个具体存储），
  在所有 passes 跑完后用 `replaceStorageInQueryTree` 把树里最左的同名 `TableNode`
  替换为指向该 storage 的新节点，并保留 alias / table modifiers。

### 4.3 委托 Planner 构建 QueryPlan（plan）

```cpp
SharedHeader InterpreterSelectQueryAnalyzer::getSampleBlock() {
  planner.buildQueryPlanIfNeeded();
  return planner.getQueryPlan().getCurrentHeader();
}

QueryPlan &InterpreterSelectQueryAnalyzer::getQueryPlan() {
  planner.buildQueryPlanIfNeeded();
  return planner.getQueryPlan();
}
```

`Planner::buildQueryPlanIfNeeded` 是真正把 `QueryTree` 翻译成 `QueryPlan` 的入口：
对 `UnionNode` 走 `buildPlanForUnionNode`，对普通 `QueryNode` 走 `buildPlanForQueryNode`，
内部完成 `executeFetchColumns / WHERE / GROUP BY / window / ORDER BY / LIMIT / projection`
等步骤的下层等价物——但这些步骤是 **planner 直接往 `QueryPlan` 里加 `IQueryPlanStep`**，
不再走旧 `InterpreterSelectQuery::executeXxx` 那一套接口。

analyzer 调用 planner 的入口非常薄，只有 `buildQueryPlanIfNeeded` + 取 plan，
所以"实际的物理执行步骤设计在哪一层"已经从 Interpreter 迁到了 Planner。

### 4.4 构建 Pipeline 与执行（pipeline / execute）

```cpp
QueryPipelineBuilder InterpreterSelectQueryAnalyzer::buildQueryPipeline() {
  planner.buildQueryPlanIfNeeded();
  auto &query_plan = planner.getQueryPlan();

  QueryPlanOptimizationSettings optimization_settings(context);
  optimization_settings.query_plan_with_parallel_replicas_builder =
      query_plan_with_parallel_replicas_builder;

  BuildQueryPipelineSettings build_pipeline_settings(context);
  query_plan.setConcurrencyControl(context->getSettingsRef()[Setting::use_concurrency_control]);

  return std::move(*query_plan.buildQueryPipeline(optimization_settings,
                                                  build_pipeline_settings));
}

BlockIO InterpreterSelectQueryAnalyzer::execute() {
  auto pipeline_builder = buildQueryPipeline();
  BlockIO result;
  result.pipeline = QueryPipelineBuilder::getPipeline(std::move(pipeline_builder));
  if (!select_query_options.ignore_quota &&
      select_query_options.to_stage == QueryProcessingStage::Complete)
    result.pipeline.setQuota(context->getQuota());
  return result;
}
```

- `buildQueryPipeline`：先确保 plan 构好，然后用 `QueryPlanOptimizationSettings`
  跑 **plan-level 优化**（谓词下推、投影优化、`CreatingSets`、并行副本备选计划等），
  再生成 `QueryPipelineBuilder`。
- `query_plan_with_parallel_replicas_builder`：以闭包形式持有"重新跑一遍 analyzer"
  所需的全部输入，是为了在 plan 优化阶段**按需**尝试生成"并行副本版本的 plan"。
  内部由 `buildQueryPlanForAutomaticParallelReplicas` 实现：
  - 检查 `parallel_replicas_local_plan` / `cluster_for_parallel_replicas` 开关，
  - 跳过 `SECONDARY_QUERY`，
  - 关闭 `build_sets`、`query_plan_optimize_primary_key`、`optimize_projection`
    （避免和原计划重复或互相干扰）。
- `execute()`：把 builder 转成可运行的 `QueryPipeline`，
  在 `to_stage == Complete` 且未忽略配额时附加 quota。

## 五、关键静态接口：纯分析路径

```cpp
static SharedHeader getSampleBlock(const ASTPtr&, const ContextPtr&, const SelectQueryOptions&);
static SharedHeader getSampleBlock(const QueryTreeNodePtr&, const ContextPtr&, const SelectQueryOptions&);
static std::pair<SharedHeader, PlannerContextPtr>
getSampleBlockAndPlannerContext(const QueryTreeNodePtr&, const ContextPtr&, const SelectQueryOptions&);
```

实现里都会强制把 `select_query_options.only_analyze = true`，然后构造一个临时 interpreter
跑到 "拿到 `QueryPlan` 的 current header" 为止。这是 **不执行、只分析** 的入口，
被 `StorageView` / 子查询分析 / 列推断等场景大量使用。

## 六、与上下游的关系

```
                  ┌──────────────────────────────────────────────────────────┐
                  │                  InterpreterSelectQueryAnalyzer           │
                  │                                                          │
   AST  ─────────►│  normalizeAndValidateQuery                               │
                  │       │                                                  │
                  │       ▼                                                  │
                  │  buildQueryTree (AST → QueryTree)                        │
                  │       │                                                  │
                  │       ▼                                                  │
                  │  QueryTreePassManager:                                   │
                  │    - resolve (always)                                    │
                  │    - optimize passes (skipped on shard / create view)    │
                  │       │                                                  │
                  │       ▼                                                  │
                  │  replaceStorageInQueryTree (optional)                    │
                  │       │                                                  │
                  │       ▼                                                  │
                  │  Planner.buildQueryPlanIfNeeded                          │
                  │       │                                                  │
                  │       ▼                                                  │
                  │  QueryPlan (+ plan-level optimizations)                  │
                  │       │                                                  │
                  └───────┼──────────────────────────────────────────────────┘
                          ▼
                  QueryPipelineBuilder → QueryPipeline → Executors
```

- **与 Parser**：消费 AST，自己不解析；
- **与 Analyzer 模块（`src/Analyzer`）**：通过 `buildQueryTree` + `QueryTreePassManager` 完成语义分析；
- **与 Planner**：把 "已经 analyze 过的 QueryTree" 交给 `planner`，由它生成 `QueryPlan`；
- **与并行副本**：以闭包形式预留"备选并行副本计划"的构建路径，
  在 plan-level 优化时按需触发；
- **与 Storage**：本类只通过 `replaceStorageInQueryTree` 把已解析的 `TableNode`
  指向具体 storage；真正的读取由 `Planner` 生成 `ReadFromMergeTree` 等 step 完成。

## 七、与旧 `InterpreterSelectQuery` 的差异

| 维度 | `InterpreterSelectQuery`（旧） | `InterpreterSelectQueryAnalyzer`（新） |
|------|--------------------------------|----------------------------------------|
| 语义模型 | 直接基于 AST + `TreeRewriter` + `ExpressionAnalyzer` | 基于 `QueryTree` + `QueryTreePassManager` |
| 执行阶段实现位置 | 自身的 `executeWhere/Aggregation/...` | 全部下沉到 `Planner` |
| 类规模 | 大、状态多（`syntax_analyzer_result`、`query_info`、`analysis_result`、`storage_snapshot` 等） | 小、状态薄（`query_tree` + `planner`） |
| 子查询/CTE 处理 | 通过 `Pipe input_pipe` 等多个构造重载 | 由 `QueryTree` 自身的节点体系承载 |
| only-analyze 路径 | 多入口、零散 | 统一通过 `only_analyze` 选项 + 静态 `getSampleBlock` |
| 并行副本备选计划 | 内嵌在执行流程 | 通过 `query_plan_with_parallel_replicas_builder` 闭包延迟生成 |

直观结论：**新版 interpreter 把"语义分析"和"执行计划"完全交给了
`Analyzer` 和 `Planner` 两个模块，自己只负责协调**。

## 八、阅读顺序建议

1. `InterpreterSelectQueryAnalyzer.h`：先看类骨架，理解它只是壳；
2. `InterpreterSelectQueryAnalyzer.cpp`：
   - `normalizeAndValidateQuery`、`buildContext`、`buildQueryTreeAndRunPasses` 三个 helper；
   - 三种构造路径的差异；
   - `execute / buildQueryPipeline / getSampleBlock` 三个出口；
3. `Planner.h` / `Planner.cpp`：理解 `buildPlanForQueryNode` / `buildPlanForUnionNode` 怎么消费 query tree；
4. `Analyzer/QueryTreeBuilder.*`、`Analyzer/QueryTreePassManager.*`、`Analyzer/Passes/*`：
   理解 passes 都做了什么；
5. 与 `InterpreterSelectQuery.*`（旧路径）做对照阅读，体会两条 pipeline 的边界差异。

---

> 文档生成时间: 2026-05-19
> 基于 `src/Interpreters/InterpreterSelectQueryAnalyzer.{h,cpp}` 与 `src/Planner/Planner.h`
