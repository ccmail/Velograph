---
description: 'Runtime walkthrough for GQL query execution'
sidebar_label: 'GQL Runtime Flow'
sidebar_position: 83
slug: /development/graph/gql-runtime-flow
title: 'GQL Runtime Flow'
doc_type: 'reference'
---

# `GQL` Runtime Flow

这篇文档按 review 代码时的阅读顺序说明一条 `GQL` 查询如何从
`executeQuery` 进入 parser、interpreter、`QueryPlan`，最后进入
`QueryPipeline`。

重点先说清楚：当前代码是一个 `GQL AST -> QueryPlan` 的 direct planner MVP。
它还没有独立的 `GQLAnalyzer`、`BoundGQLQuery` 或 logical graph plan IR。文档里提到
`GQLPlanner`、`ClauseSequencePlanner`、`PatternBinder` 等名字时，它们只是当前
实现文件的映射，不代表推荐的长期架构分层。

## 总览

从 ClickHouse 风格看，推荐心智模型应该是：

```text
parse
  -> analyze / bind
  -> plan
  -> optimize QueryPlan
  -> build QueryPipeline
  -> execute processors
```

`GQL` 的目标分层也应该接近：

```text
GQL AST
  -> BoundGQLQuery / Graph Query IR
  -> Logical graph plan
  -> QueryPlan
  -> QueryPipeline
  -> Processors / Sources
```

当前代码还没有中间两层，所以实际 runtime 链路更像：

```text
executeQuery
  -> executeQueryImpl
    -> parse as Dialect::gql
      -> ParserGQLQuery
      -> GQL AST
    -> InterpreterFactory
      -> InterpreterGQLQuery
        -> current direct GQL planner helpers
          -> QueryPlan with Graph::MatchStep
        -> QueryPlan::buildQueryPipeline
          -> MatchStep::initializePipeline
          -> Graph::MatchSource
```

这样读会少绕一点：`InterpreterGQLQuery` 下面这批 planner/binder/spec-builder 文件是在帮 direct planner 搭 `QueryPlan`，不是新的 ClickHouse pipeline 阶段。

## 当前实现 vs 目标分层

| 阶段 | 当前实现 | 目标边界 |
|------|----------|----------|
| Parse | `ParserGQLQuery` 调 `GQLParserUtils::parseStatement`，再由 `GQLParseTreeVisitor` 生成 `GQL*` AST。 | 保持现状，parser 只产语法 / 语义 AST，不直接规划执行。 |
| Analyze / bind | 还没有独立层。`PlanScope`、`PatternBinder` 和 expression planner helper 临时承担了一部分 binding / type / name handling。 | 引入 `GQLAnalyzer` / binder，输出 `BoundGQLQuery` 或 graph query IR。 |
| Plan | `InterpreterGQLQuery` 调 current direct planner helpers，直接从 `GQL*` AST 生成 ClickHouse `QueryPlan`。 | `GQLPlanner` 消费 bound IR / logical graph plan，再生成 `QueryPlan`。 |
| Physical graph spec | `MatchSpecBuilder` 把当前 `MatchPlan` 转成 `Graph::MatchSpec`。 | 从 logical match plan 生成明确的 physical `MatchSpec` / graph source contract。 |
| QueryPipeline | `QueryPlan::buildQueryPipeline` 触发 `MatchStep::initializePipeline`。 | 保持 ClickHouse 原生模式：只有 `QueryPlan` build pipeline 时才进入真正 `QueryPipeline`。 |
| Execution source | `Graph::MatchStep` 直接创建占位 `Graph::MatchSource`，当前 source 发出零行。 | 由明确的 graph storage / planner contract 接入真实 source pipe。 |

## 1. `executeQuery` 进入 parser 分支

入口在 `src/Interpreters/executeQuery.cpp`。

`executeQuery` 做 server 状态、CPU overload、结果格式等外围处理，然后把 query
buffer、`Context`、`QueryFlags` 和 `QueryProcessingStage` 交给 `executeQueryImpl`。

`executeQueryImpl` parse 阶段按 `dialect` 选择 parser。`Dialect::gql` 分支做三件事：

1. 检查 `allow_experimental_gql_dialect`。
2. 构造 `ParserGQLQuery`。
3. 调用 `parseGQLQuery`，把完整 query span 交给 `GQL` parser。

普通 ClickHouse `ParserQuery` 不会 prefix-sniff `MATCH` / `USE` / graph-shaped
`SELECT`。只有显式 `Dialect::gql` 才会走 `ParserGQLQuery`。

## 2. `ParserGQLQuery` 生成 `GQL*` AST

`ParserGQLQuery::parseStatementText` 在 `src/Parsers/graph/ParserGQLQuery.cpp`：

```text
ParserGQLQuery::parseStatementText
  -> GQLParserUtils::parseStatement
  -> GQLParseTreeVisitor::visit
```

`GQLParserUtils::parseStatement` 在 `src/Parsers/graph/GQLParserUtils.cpp`。它用
`gqlStatement` 作为 production entry。对应 grammar 在
`src/Parsers/graph/grammar/GQL.g4`：

```text
gqlStatement
  : statement SEMICOLON* EOF
```

这层的职责是完整输入校验：允许结尾分号，但不允许后面还有未消费 token。visitor
收到的是内部 `statement` 节点，所以现有 visitor 不需要关心 EOF 包装。

对 `MATCH (n) RETURN n` 这样的线性 query，主要 parse tree 路径是：

```text
statement
  -> queryStatement
  -> linearQueryStatement
  -> simpleLinearQueryStatement
  -> simpleQueryStatement+
```

相关 visitor 文件：

- `GQLParseTreeVisitorQuery.cpp` 负责 query root、`SELECT`、线性 clause 串和
  `CALL` 等 query-level 结构。
- `GQLParseTreeVisitorPattern.cpp` 负责 `MATCH`、graph pattern、node / edge /
  path pattern、`KEEP`、`YIELD` 和 pattern-level `WHERE`。
- `GQLParseTreeVisitorProjection.cpp` 负责 `RETURN`、`ORDER BY`、`LIMIT` 等结果
  clause。
- `GQLParseTreeVisitorExpression.cpp` 负责 `GQLExpr` 以及可复用的 ClickHouse
  native expression AST。

对 `MATCH`，`visitSimpleMatchStatement` 会访问 `graphPatternBindingTable`，得到临时的
`PatternBindingTable`，再通过 `makeMatchClause` 生成 `GQLMatchClause`。

最终 query root 主要是：

- `GQLSingleQuery`：线性 query 和 DML clause 序列。
- `GQLCombinedQuery`：`UNION`、`EXCEPT`、`INTERSECT` 等 set query。

## 3. `InterpreterFactory` 进入 `InterpreterGQLQuery`

parse 完成后，`executeQueryImpl` 进入通用 interpreter 创建逻辑：

```text
out_ast
  -> InterpreterFactory::get
  -> interpreter->execute
```

`InterpreterFactory::get` 在 `src/Interpreters/InterpreterFactory.cpp` 里根据 AST 类型选择
interpreter。当前 `GQL` 条件是：

```text
GQLSingleQuery or GQLCombinedQuery
  -> "InterpreterGQLQuery"
```

`InterpreterGQLQuery` 在 `src/Interpreters/InterpreterGQLQuery.cpp`。它本身保持得比较薄：

```text
InterpreterGQLQuery::execute
  -> buildQueryPlan
    -> GQL::buildGQLQueryPlan
  -> query_plan.buildQueryPipeline
  -> QueryPipelineBuilder::getPipeline
```

这点是合理的：`InterpreterGQLQuery` 应该只组织 parser AST 到 plan / pipeline 的主流程。
当前容易晕的地方不是 interpreter 本体，而是 `GQL::buildGQLQueryPlan` 下面的 helper 命名。

## 4. 当前 direct planner helpers 映射

当前 `src/Interpreters/GQL` 下的 helper 直接从 `GQL*` AST 搭 `QueryPlan`。它们不是一套完整
IR 分层，review 时建议按下面的职责理解：

| 当前文件 / helper | 当前职责 | 更清晰的长期边界 |
|-------------------|----------|------------------|
| `GQLPlanner` | root dispatch helper：识别 `GQLSingleQuery` / `GQLCombinedQuery`，组合 child plan。 | 收敛进 `GQLPlanner` / `GQLPlanBuilder` 的 root planning。 |
| `GQLPlanBuilder` | 持有 `Context` 和 `PlanScope`，把 single query 交给 clause sequence helper。 | 作为 `GQLPlanBuilder` 主体更自然。 |
| `ClauseSequencePlanner` | 解释线性 clause 顺序，决定何时建立 source、何时处理 post-source clause。 | 更像 `ClausePlanner` / clause sequence planner。 |
| `SourcePlanner` | 当前 source planning：`MATCH`、`SELECT FROM`、source-free single-row source。 | 更像 source planner，而不是独立 IR planning 层。 |
| `PostSourceClausePlanner` | 当前 post-source clause dispatch：source 已经存在后，把 clause 继续加到 `QueryPlan`。 | 它不是 ClickHouse `QueryPipeline` 构建阶段，只负责 source 已存在后的 clause dispatch。 |
| `MatchPlanner` | 当前 `MATCH` source planner：生成 `MatchPlan`、`MatchSpec`、`Graph::MatchStep`，并追加 `MATCH WHERE` filter。 | 更像 `MatchPlanner`，输入应是 bound pattern / logical match plan。 |
| `PatternBinder` | 从 parser pattern AST 提取 `NodeBinding` / `EdgeBinding` / `PathBinding` / `MatchPlan`。 | 应拆成 `PatternBinder` 或 `BoundGraphPattern` builder。 |
| `MatchSpecBuilder` | 把 `MatchPlan` 转成 `Graph::MatchSpec`。 | 更像 `MatchSpecBuilder`，位于 logical match plan 到 physical graph source spec 之间。 |

因此，当前实现可以概括为：

```text
GQL AST
  -> direct planner helpers
  -> QueryPlan
```

不要把这批 helper 名字理解成目标架构里的多级 planning IR。

## 5. `MATCH` 如何变成 `Graph::MatchStep`

以 `MATCH (n) WHERE n.id = 1 RETURN n LIMIT 1` 为例，当前 planner helper 的关键步骤是：

```text
GQLSingleQuery clauses
  -> clause sequence helper buffers MATCH as source
  -> source helper flushes MATCH clauses
  -> match helper builds MatchPlan
  -> MatchSpecBuilder builds Graph::MatchSpec
  -> QueryPlan adds Graph::MatchStep
  -> MATCH WHERE is also appended as FilterStep
  -> RETURN / LIMIT add normal QueryPlan steps
```

几点边界要分清：

- `MATCH ... WHERE ...` 会保存在 `MatchSpec` 里，未来 graph storage 可以 inspect 或 push down。
- 同一个 predicate 当前也会在 `MatchStep` 后面通过 `planWhereClause` 变成 ClickHouse
  `FilterStep`，保证现有 pipeline 语义可执行。
- 连续 `MATCH` 当前共享一个 `Graph::MatchStep`，per-clause 约束保存在
  `MatchSpec::clauses`。
- `OPTIONAL MATCH` 可以被 parser 和 spec 保留，但执行阶段仍 fail closed，因为
  outer-match / null-extension 语义还没实现。

`Graph::MatchSpec` 在 `src/Processors/QueryPlan/Graph/MatchSpec.h`，包含 graph
reference、per-clause specs、path specs、node / edge variable、label / property /
predicate AST、quantifier、`KEEP` 和 `YIELD` 等信息。

## 6. `QueryPlan` 再进入真正 `QueryPipeline`

真正的 pipeline 构建发生在 `InterpreterGQLQuery::execute` 调
`QueryPlan::buildQueryPipeline` 之后。

```text
QueryPlan::buildQueryPipeline
  -> ISourceStep::updatePipeline
  -> MatchStep::initializePipeline
    -> source_factory->createReader
    -> MatchSource(header, match_spec, reader)
    -> pipeline.init(Pipe(MatchSource))
```

这也是为什么 `PostSourceClausePlanner` 这个名字容易误导：它在 `QueryPlan` planning helper 阶段
做 post-source clause dispatch，并没有构建 ClickHouse `QueryPipeline`。

`Graph::MatchSource` 在 `src/Processors/Sources/Graph/MatchSource.cpp`。现在它只是占位 source，
`generate` 返回空 `Chunk`。后续接 storage 时，需要先设计明确的 graph storage / planner
contract，再把真实 source pipe 接进 `MatchStep`；不要为了提前占位引入半成品 planner-wide
service bag。

## 代码阅读路径

review 当前代码时可以按这个顺序跳：

1. `src/Interpreters/executeQuery.cpp`
   - `executeQuery`
   - `executeQueryImpl`
   - `Dialect::gql` parse branch
   - `InterpreterFactory` 的 `get`
   - `interpreter->execute`
2. `src/Parsers/graph/ParserGQLQuery.cpp`
   - `ParserGQLQuery::parseStatementText`
   - `parseGQLQuery`
3. `src/Parsers/graph/GQLParserUtils.cpp`
   - `GQLParserUtils::parseStatement`
4. `src/Parsers/graph/grammar/GQL.g4`
   - `gqlStatement`
   - `matchStatement`
   - `simpleMatchStatement`
5. `src/Parsers/graph/visitor/GQLParseTreeVisitorPattern.cpp`
   - `visitSimpleMatchStatement`
   - `visitGraphPattern`
   - `makeMatchClause`
6. `src/Parsers/graph/visitor/GQLParseTreeVisitorProjection.cpp`
   - `visitReturnStatementBody`
   - `visitOrderByAndPageStatement`
7. `src/Interpreters/InterpreterFactory.cpp`
   - `GQLSingleQuery` / `GQLCombinedQuery` -> `InterpreterGQLQuery`
8. `src/Interpreters/InterpreterGQLQuery.cpp`
   - `execute`
   - `buildQueryPlan`
9. `src/Interpreters/GQL/GQLPlanner.cpp`
   - `buildGQLQueryPlan`
   - `buildSingleQueryPlan`
10. `src/Interpreters/GQL/GQLPlanBuilder.cpp`
    - `GQLPlanBuilder::buildSingleQuery`
11. `src/Interpreters/GQL/ClauseSequencePlanner.cpp`
    - `ClauseSequencePlannerState::plan`
    - `SourceClauseBuffer` flush timing
12. `src/Interpreters/GQL/SourcePlanner.cpp`
    - `SourceClauseBuffer::flush`
    - `tryPlanStandaloneSourceClause`
13. `src/Interpreters/GQL/MatchPlanner.cpp`
    - `planMatchClauseSequence`
14. `src/Interpreters/GQL/PatternBinder.cpp`
    - `bindMatchClause`
    - `bindPathPattern`
15. `src/Interpreters/GQL/MatchSpecBuilder.cpp`
    - `buildMatchSpec`
16. `src/Processors/QueryPlan/Graph/MatchStep.cpp`
    - `MatchStep::MatchStep`
    - `MatchStep::makeHeader`
    - `MatchStep::initializePipeline`
17. `src/Processors/Sources/Graph/MatchSource.cpp`
    - `MatchSource::generate`

这张列表是当前代码导航，不是目标架构图。

## 后续重构建议

本轮先做命名和边界收敛，不引入新的 C++ IR。后续如果继续重构，建议避免一次性引入过重 IR：

- 保留 `InterpreterGQLQuery` 的薄入口，让它只组织 analyze / plan / execute。
- 本轮已经把泛泛的 direct helper 命名收敛到 planner/binder/spec builder 语义。
- 优先处理 `PostSourceClausePlanner` 和 `QueryPipeline` 的边界说明，避免把 post-source clause dispatch 读成 pipeline 构建。
- 让 `PatternBinder` 逐步输出更明确的 `BoundGraphPattern`。
- 让 `MatchSpecBuilder` 只负责 logical match plan 到 physical source spec。
- 在真实 storage 接入前，不急着把所有 clause 都拆成重型 IR；先保持 `QueryPlan` 输出行为稳定。

## 当前边界

- `MATCH` source step 形状已接入 `QueryPlan`，但真实 graph storage reader 未接入。
- `OPTIONAL MATCH`、source composition、correlated apply、catalog / DML execution 等仍是
  fail-closed 路径。
- `MATCH ... WHERE ...` 同时保存在 `MatchSpec` 和 `FilterStep` 中。
- `SELECT FROM g MATCH ...` 的 graph scope 只在 source-local scope 中生效，不会泄漏到外层
  `PlanScope`。
- `QueryPlan::buildQueryPipeline` 之后才进入真正 ClickHouse `QueryPipeline` 阶段。
