# GQL Analyzer QueryTree 风格重构计划

## 1. 目标

当前 `GQL` 执行路径还是 direct planner：`ParserGQLQuery` 生成 `GQLSingleQuery` / `GQLCombinedQuery` 等 AST 后，`InterpreterGQLQuery` 直接调用 `GQLPlanner` / `GQLPlanBuilder` / `ClauseSequencePlanner` 等 helper 生成 `QueryPlan`。

这条路径已经能复用不少 ClickHouse 执行组件，例如 `ActionsDAG`、`ExpressionStep`、`FilterStep`、`SortingStep`、`LimitStep`、`UnionStep`、`IntersectOrExceptStep`。但它还没有一个独立的 analyze / bind 阶段，因此名字解析、scope 管理、表达式类型推导、pattern binding、`MATCH` spec 构造和部分 planning 逻辑仍混在一起。

本计划选择 **QueryTree 风格方案**：在 `IQueryTreeNode` 体系下新增 `GQL` 语义节点，让 `GQL` 也有清晰的

```text
AST -> GQL QueryTree -> analyzed GQL QueryTree -> QueryPlan -> QueryPipeline
```

链路。目标不是把 `GQL` 硬套进 SQL `QueryNode`，而是在同一套 `IQueryTreeNode` 基础设施中，为 `GQL` 建立 sibling-level 的语义节点。

## 2. 对前面讨论的纠偏

### 2.1 `QueryNode` 不是通用查询根节点

之前容易误解的一点是：`QueryNode` 不是“所有查询的通用节点”，更接近 ClickHouse 新 analyzer 里的 SQL `SELECT` 语义节点。

它的固定结构是：

```text
QueryNode
  WITH
  PROJECTION
  JOIN TREE
  PREWHERE
  WHERE
  GROUP BY
  HAVING
  WINDOW
  QUALIFY
  ORDER BY
  INTERPOLATE
  LIMIT BY
  LIMIT
  OFFSET
  CORRELATED COLUMNS
```

这些槽位在 `QueryNode` 里是固定 index，并且 `QueryAnalysisPass`、SQL `Planner`、query tree visitor / validator 都默认 `QueryNode` 表示 SQL `SELECT` 形状。

因此，删掉 `final` 再继承 `QueryNode` 不是好的省事路径：

- `QueryNode` 的 child layout 是固定的，派生类不能自然加入 `USE` / `MATCH` / path pattern 等槽位。
- `clone`、`dumpTreeImpl`、`toASTImpl`、`isEqualImpl`、hash、validation、planner 都只理解现有 SQL section。
- Generic SQL analyzer 看到 `QueryNode` 会按 SQL `JOIN TREE`、`WHERE`、`GROUP BY` 语义处理，不能安全理解 `MATCH (a)-[r]->(b)`。
- 把 `MATCH` 塞进 `QueryNode::getJoinTree` 会把 graph pattern 伪装成 SQL table expression，后续会持续付出适配成本。

正确理解是：

```text
IQueryTreeNode
  QueryNode              -- SQL SELECT-like query
  UnionNode              -- SQL UNION / INTERSECT / EXCEPT
  JoinNode               -- SQL join tree node
  FunctionNode           -- scalar / aggregate / window expression
  SortNode               -- order item
  ...
  GQLLinearQueryNode     -- 新增，表示 GQL clause pipeline
  GQLMatchNode           -- 新增，表示 GQL graph source clause
  GQLPathPatternNode     -- 新增，表示 GQL path pattern
  ...
```

`GQL` 应该扩展 `IQueryTreeNode` 体系，而不是继承或改造 SQL `QueryNode`。

### 2.2 `clauses: ListNode` 的含义

`GQL` 的单查询不是 SQL `SELECT` 那种固定槽位模型，而是一条 ordered clause pipeline。比如：

```gql
MATCH (n)
WHERE n.age > 18
LET x = n.age + 1
RETURN x
ORDER BY x
LIMIT 10
```

以及：

```gql
MATCH (a)
MATCH (b)
RETURN a, b
```

这里 clause 顺序是语义的一部分。`GQLLinearQueryNode` 里的 `steps` / `clauses` 不是为了照搬 parser AST，而是为了保存 **分析后的有序语义步骤**。

如果走 QueryTree 风格，最自然的表示是：

```text
GQLLinearQueryNode
  steps: ListNode
    GQLUseNode
    GQLMatchNode
    GQLFilterNode
    GQLLetNode
    GQLReturnNode
    GQLOrderByNode
    GQLPageNode
```

这里用 `ListNode` 只是复用现有 ordered child container；重点不是名字叫 `clauses`，而是 `GQL` linear query 必须保留 ordered semantic steps。

### 2.3 QueryTree 不是 AST 的机械复制

新增节点应按大语义切分，不按 grammar 细碎节点切分。

保留为 `GQL` 专有节点的部分：

- `MATCH`
- graph scope / `USE`
- path pattern
- node pattern
- edge pattern
- edge direction
- label expression
- path prefix / path search mode
- path alternation
- `KEEP`
- `YIELD`
- optional match / optional block

尽量复用 ClickHouse 现有 QueryTree 节点的部分：

- 普通 identifier：分析前可用 `IdentifierNode`
- 已解析列 / 变量：分析后可用 `ColumnNode`
- 常量：`ConstantNode`
- 标量函数和运算：`FunctionNode`
- 表达式列表：`ListNode`
- 排序项：`SortNode`

复用条件是“语义相同”。例如 `abs(x)`、`x + 1`、`ORDER BY x DESC` 可以复用 ClickHouse 表达式节点；但 `n.age` 如果代表 graph element property，在真实 graph value 类型明确前不应盲目等价成普通 SQL subcolumn。

## 3. 为什么选择 QueryTree 风格

我们本可以先做一个轻量 `BoundGQLQuery` struct IR，再由 planner 消费它。但这份计划选择 QueryTree 风格，原因是：

1. **贴近 ClickHouse 新 analyzer 的组织方式。**  
   `IQueryTreeNode` 已经提供 clone、hash、dump、visitor、pass manager 等基础设施。`GQL` 用 sibling nodes 接入，比维护一套完全独立 IR 更容易被 ClickHouse reviewer 理解。

2. **能逐步复用表达式分析。**  
   `GQL` 的 graph source 是专有的，但 `RETURN`、`FILTER`、`ORDER BY`、`LIMIT` 等后续 transform 可以尽量共享 `FunctionNode`、`SortNode`、`ConstantNode`、`ActionsDAG` 的思想和部分工具。

3. **有利于后续 `EXPLAIN` / debug。**  
   QueryTree 风格节点可以自然支持 `dumpTreeImpl`，后续也可以让 `EXPLAIN` 展示 analyzed `GQL` tree。

4. **避免污染 SQL `QueryNode`。**  
   `GQLLinearQueryNode` 作为 sibling node，可以保持 `GQL` 语义完整，同时不影响 SQL analyzer 对 `QueryNode` 的假设。

5. **保留 ordered pipeline。**  
   `GQL` 的 linear query 可以用 `ListNode` 存储 ordered semantic steps，不需要把它压扁进 SQL 固定槽位。

## 4. 目标链路

```text
executeQuery
  -> Dialect::gql branch
  -> ParserGQLQuery
  -> GQL AST
       GQLSingleQuery
       GQLCombinedQuery
       GQLSubquery
  -> GQLQueryTreeBuilder
       GQLLinearQueryNode
       GQLCombinedQueryNode
       GQLSubqueryNode
  -> GQLQueryTreePassManager
       name / binding analysis
       type analysis
       pattern normalization
       predicate classification
  -> GQLQueryTreePlanner
       Graph::MatchSpec
       QueryPlan steps
  -> QueryPlan::buildQueryPipeline
  -> Graph::MatchStep / Graph::MatchSource / ordinary processors
```

这里的 `GQL QueryTree -> analyzed GQL QueryTree` 不是指两套不同 IR，也不是必须重新构造一棵新树。更准确地说，它表示同一套 `GQL` QueryTree 的两个逻辑状态：

```text
AST
  -> initial / unresolved GQL QueryTree
  -> run GQL analysis passes
  -> resolved / analyzed GQL QueryTree
  -> QueryPlan
```

`GQLQueryTreeBuilder` 只负责把 parser AST 转成 analyzer 层的语义结构，例如 `GQLSingleQuery` 变成带 ordered `steps` 的 `GQLLinearQueryNode`。`GQLQueryTreePassManager` 再在这棵树上做 name resolution、binding、type analysis、visible bindings 推导和 predicate classification。实现上可以原地标注或改写同一棵树；文档里拆成两步，是为了强调 builder 阶段不要偷偷做语义绑定或 planning。

命名建议：QueryTree 节点使用 `GQLLinearQueryNode`，不要使用 `GQLSingleQueryNode`。`GQLSingleQuery` 已经是 parser AST 类名，换一个名字可以减少 parser AST 和 analyzed QueryTree 之间的混淆。

## 5. 建议的节点集合

### 5.1 查询根节点

```text
GQLLinearQueryNode : IQueryTreeNode
  steps: ListNode
```

表示一条有序的 `GQL` clause pipeline。它是 parser `GQLSingleQuery` 在 QueryTree 层的对应节点，但里面应该存放语义步骤，而不是原始 grammar 节点。

```text
GQLCombinedQueryNode : IQueryTreeNode
  queries: ListNode<GQLLinearQueryNode | GQLCombinedQueryNode>
  operators: vector<GQLCombinedOperator>
```

第一阶段不要直接复用 `UnionNode`。现有 `UnionNode` 假设 child 是 SQL `QueryNode` / `UnionNode`，相关分析 helper 也会按 SQL projection columns 计算。`GQL` combined query 应该先显式保留 `UNION ALL` / `UNION DISTINCT` / `EXCEPT` / `INTERSECT` 语义，再在 planning 阶段 lower 到现有 `UnionStep` / `IntersectOrExceptStep`。

```text
GQLSubqueryNode : IQueryTreeNode
  kind: inline_call_body | source_subquery
  body: GQLLinearQueryNode | GQLCombinedQueryNode
  binding definitions
  AT schema / NEXT YIELD metadata
```

用于结构化保存 inline `CALL` body 和 source subquery。二者可以先共享一个节点实现，但必须显式带 `kind`：`inline_call_body` 来自 `CALL { ... }` 里的 nested procedure specification，会携带 `AT schema`、binding definitions 和 `NEXT YIELD` 规则；`source_subquery` 来自 source expression position，例如 `SELECT ... FROM { ... }`。不要让 analysis pass 只能靠调用上下文猜测它是哪一种。

注意：named `CALL proc(...) YIELD ...` 在 query execution path 里没有 inline body，不应该被误建成 `inline_call_body`。如果后续 DDL procedure definition 也复用这套 QueryTree，需要再单独扩展 kind，而不是把它混进 source subquery。

### 5.2 Source 和 graph scope 节点

```text
GQLUseNode : IQueryTreeNode
  graph_expression
```

更新后续 source clauses 使用的 active graph scope。

```text
GQLMatchNode : IQueryTreeNode
  optional
  match_mode
  paths: ListNode<GQLPathPatternNode>
  where
  keep
  yield
```

`optional` 只表示 `OPTIONAL MATCH ...` 这种修饰单个 match clause 的简单形态。

这里的 `match_mode` 指 parser `GraphMatchMode`：repeatable elements、repeatable element bindings、different edges 或 different edge bindings。不要把它和 walk / trail / simple / acyclic 这类 path mode 混在一起。

`MATCH ... WHERE ...` 必须保留在 `GQLMatchNode` 上，后续复制到 `Graph::MatchSpec::where_clause`。在 graph storage 真正消费 predicate 前，也可以同时 plan 成 post-source `FilterStep`。

```text
GQLOptionalBlockNode : IQueryTreeNode
  body: GQLLinearQueryNode
```

`OPTIONAL { ... }` / `OPTIONAL ( ... )` 是 block-level 构造，不应该塞进 `GQLMatchNode`。当前 parser AST 临时把 optional operand block 放在 `GQLMatchClause::optional_operand_block`，但 QueryTree 层应拆成独立 step。当前 grammar 的 `matchStatementBlock` 只包含 one or more match statements；如果后续 grammar 扩展成更完整的 sub-pipeline，这个节点仍然可以保持 `body: GQLLinearQueryNode` 的形状。

### 5.3 Path pattern 节点

```text
GQLPathPatternNode : IQueryTreeNode
  path_variable
  prefix
  expression: GQLPathTermNode | GQLPathAlternationNode | GQLSimplifiedPathPatternNode
```

`path_variable` 对应 `p = (a)-[r]->(b)` 这样的语法。

`prefix` 不是 path variable。它表示 path mode / path search prefix，例如 `ANY SHORTEST`，或未来 walk / trail / simple / acyclic 这类约束。

`expression` 的 QueryTree child 类型必须收敛在这个集合内。Builder 遇到 parser 的 simplified path pattern 时，应建成 `GQLSimplifiedPathPatternNode`，不要强行展开成 `GQLPathTermNode`。simplified path pattern 包含 union、multiset alternation、conjunction、direction override、repetition 等表达式语义，直接塞进 classic node / edge chain 容易丢信息。第一阶段 planner 可以对 `GQLSimplifiedPathPatternNode` fail closed；等 graph expansion semantics 明确后，再由 analysis 或专门 lowering pass 选择性规范化成 `GQLPathTermNode` / `GQLPathAlternationNode`。

```text
GQLPathTermNode : IQueryTreeNode
  factors: ListNode<GQLNodePatternNode | GQLEdgePatternNode>
```

表示经典的 node / edge / node 交替链。

```text
GQLPathAlternationNode : IQueryTreeNode
  kind: union | multiset alternation
  alternatives: ListNode<GQLPathTermNode | GQLPathAlternationNode>
```

这是 path alternation，不是 label alternation。label boolean logic 应该放在 `GQLLabelExpressionNode`。

```text
GQLSimplifiedPathPatternNode : IQueryTreeNode
  default_direction
  expression: GQLSimplifiedPathExprNode
  quantifier
```

```text
GQLSimplifiedPathExprNode : IQueryTreeNode
  kind: label | group | concatenation | conjunction | union | multiset_alternation | negation | direction_override | repetition
  text
  direction
  operand
  operands
  quantifier
```

这里的字段对应当前 parser AST 的 `GQLSimplifiedPathPattern` / `GQLSimplifiedPathExpr` 结构。QueryTree 第一阶段只要求完整保留结构；不要在 builder 中提前解释 label matching、方向覆盖、重复和 path expression boolean logic。

### 5.4 Node 和 edge pattern 节点

```text
GQLNodePatternNode : IQueryTreeNode
  variable
  label_expression
  properties
  predicate
```

```text
GQLEdgePatternNode : IQueryTreeNode
  variable
  direction
  label_expression
  properties
  predicate
  quantifier
```

Edge direction 必须覆盖当前 parser 支持的所有 directions：

```text
Left
Right
Undirected
LeftOrRight
LeftOrUndirected
UndirectedOrRight
Any
```

不要把它收窄成只有 left / right / undirected。

```text
GQLQuantifierNode : IQueryTreeNode
  kind: star | plus | question | exact | range
  lower
  upper
```

在 graph expansion semantics 实现前，保留原始 lower / upper expression 或 text。不要太早把所有 quantifiers 强行折叠成 `min_count` / `max_count`。

### 5.5 Constraint 和 projection 节点

```text
GQLLabelExpressionNode : IQueryTreeNode
  kind: name | wildcard | negation | conjunction | disjunction
  operands
```

Label expressions 是 graph-specific 语义，不应该表示成普通 scalar `FunctionNode`，除非后面明确做 intentional lowering。

```text
GQLPropertyMapNode : IQueryTreeNode
  items: ListNode<GQLPropertyItemNode>
```

```text
GQLPropertyItemNode : IQueryTreeNode
  property_name
  value_expression
```

```text
GQLKeepNode : IQueryTreeNode
  path_prefix
```

`KEEP` 当前保留的是 `KEEP ANY 2 PATHS` 这类 path-selection constraints；它不是变量列表。

```text
GQLYieldNode : IQueryTreeNode
  items: ListNode<expression nodes>
  yield_variables
```

`MATCH ... YIELD ...` 会影响 graph-match output projection。Planner 应该从这个节点推导 exposed variables；存在 `YIELD` 时，`MatchStep` 只输出这些变量。

### 5.6 Post-source clause 节点

```text
GQLFilterNode : IQueryTreeNode
  predicate
```

表示 source 已存在之后的 `WHERE` / `FILTER`。

```text
GQLReturnNode : IQueryTreeNode
  projection: ListNode
  distinct
  group_by
```

Projection expressions 在语义匹配时可以复用 `IdentifierNode` / `ColumnNode` / `ConstantNode` / `FunctionNode`。

```text
GQLOrderByNode : IQueryTreeNode
  items: ListNode<SortNode>
```

当 expression semantics 匹配 ClickHouse expressions 时，用 `SortNode` 表示 scalar sort items。

```text
GQLPageNode : IQueryTreeNode
  offset
  limit
```

QueryTree 层选择把排序和分页拆开：所有 `ORDER BY` 都进入 `GQLOrderByNode`，`GQLPageNode` 只保存 `OFFSET` / `LIMIT`。虽然当前 parser AST 的 `GQLPageClause` 把 `ORDER BY`、`OFFSET`、`LIMIT` 打包在一起，builder 应该把它拆成可选 `GQLOrderByNode` 加可选 `GQLPageNode`，这样 planner lowering 更直接对齐 `SortingStep` + `LimitStep` 两步。

```text
GQLLetNode
GQLForNode
GQLCallNode
GQLFinishNode
```

这些节点可以按当前 planner behavior 的支持范围逐步添加。第一阶段 QueryTree 应优先覆盖已有 tests 和 direct planner code 已经覆盖的 clauses。

## 6. 表达式复用规则

当语义完全一致时，复用现有 ClickHouse QueryTree 表达式节点：

- `IdentifierNode`：用于未解析的 variable / column references。
- `ColumnNode`：在 `GQL` analysis 把 binding 解析成 output column 后使用。
- `ConstantNode`：用于普通常量。
- `FunctionNode`：用于能干净映射到 ClickHouse functions 的 scalar operators 和 functions。
- `SortNode`：用于普通 scalar sort items。
- `ListNode`：用于有序列表。

当语义不完全一致时，保留 `GQL` 专有 expression nodes 或 metadata：

- graph value type semantics 明确前的 graph element property access，例如 `GQLPropertyAccessNode`。
- label expressions。
- graph expressions。
- binding table expressions。
- path constructors。
- dynamic parameters。
- type predicates 和 graph-specific type constructs。

这和现有 parser 指导原则一致：只有语义相同时才复用 ClickHouse-native expression AST；`GQL` 专有概念保留在专门的 `GQL*` nodes 中。

## 7. Analysis passes

使用专用的 `GQLQueryTreePassManager`。不要直接在 `GQLLinearQueryNode` 上运行 generic SQL `QueryAnalysisPass`，因为它默认输入是 SQL `QueryNode` / `UnionNode` 形状。

初始 passes：

| Pass | 责任 |
|------|----------------|
| `GQLNameResolutionPass` | 解析 graph variables、projection aliases、`LET` bindings、imported `CALL` variables 和 active graph scope。 |
| `GQLTypeAnalysisPass` | 为 scalar expressions 和 exposed bindings 分配 result types；真实 graph values 定义前使用 placeholder graph element types。Graph element property access 在此阶段仍保持为 `GQL` 专有 expression node，例如 `GQLPropertyAccessNode`，不要 lower 到 `FunctionNode`；只有在 graph storage 明确 property access 的 runtime semantics 后，才能在后续 pass 中选择性 lower。 |
| `GQLPatternAnalysisPass` | 校验 path / node / edge pattern 形状，保留 labels / properties / predicates，并推导 available variables。当前 `PatternBinder` 的结构化 binding 职责应迁移到这里。 |
| `GQLPredicateClassificationPass` | 在不改变语义的前提下，把 predicates 分类为 graph-source pushdown candidates 或 post-source filters。第一版只标记 trivially safe / unsafe；涉及 graph element property access 的 predicate 在语义模型落地前一律保持 post-source filter。 |
| `GQLProjectionAnalysisPass` | 为 `RETURN`、`SELECT`、`YIELD`、`FINISH` 和 combined query children 推导 output columns。 |

重点：`MATCH ... WHERE ...` 不能简单“抽到 top-level WHERE”。它必须保留在 match clause 上，为未来 graph storage pushdown 留通道；planner 同时可以加 `FilterStep` 来保持当前行为。

## 8. Planning 策略

新的 `GQL` planner 应该消费 analyzed `GQL` QueryTree nodes，并生成与当前 direct planner 等价的 `QueryPlan`。

高层映射：

| QueryTree node | Planning 结果 |
|----------------|-----------------|
| `GQLUseNode` | 更新 planner graph scope。 |
| one or more consecutive `GQLMatchNode` | 构造带 `MatchClauseSpec` sequence 的 `Graph::MatchSpec`，再添加一个 `Graph::MatchStep`。 |
| `GQLFilterNode` | 添加 `FilterStep`。 |
| `GQLReturnNode` | 添加 `ExpressionStep`，以及可选 aggregation / `DistinctStep`。 |
| `GQLOrderByNode` | 添加 `SortingStep`。 |
| `GQLPageNode` | 添加 `LimitStep` / offset handling。 |
| `GQLOptionalBlockNode` | 保留 block-level optional semantics；执行仍可 fail closed，直到 outer-match / null-extension semantics 实现。 |
| `GQLCombinedQueryNode` | 构造 child plans，再使用 `UnionStep` 或 `IntersectOrExceptStep`。 |

迁移期间应尽量复用或改造当前 direct planner modules：

- `MatchSpecBuilder`
- `ExpressionPlanner`
- `ClausePlanner`
- `AggregationPlanner`
- `SourceCompositionPlanner`
- `CallPlanner`
- `SubqueryPlanner`
- `ApplyPlanner`

`PatternBinder` 不应该继续作为 planner 阶段的长期依赖。它现在承担的 path / pattern binding 职责应迁移进 `GQLPatternAnalysisPass`；迁移期间可以临时复用内部逻辑，但最终 planner 应消费 analyzed QueryTree，再由 `MatchSpecBuilder` 从已绑定 tree 构造 `Graph::MatchSpec`。

第一批 QueryTree 阶段不要删除 `GQLPlanBuilder`、`ClauseSequencePlanner`、`SourcePlanner` 或 `PostSourceClausePlanner`。先构建能产出等价 plan 的 QueryTree-backed path，覆盖充分后再移除 direct planner helpers。

## 9. `MatchStep`、Pushdown 和 Apply

### 9.1 `MatchStep`

初始 QueryTree 重构阶段，保持 `Graph::MatchStep` 作为 `ISourceStep`。

把它改成 `ITransformingStep` 本身并不能实现 correlated subqueries。correlated nested graph source 需要显式 apply 语义：outer row binding import、inner source execution 和 result combination。

### 9.2 Predicate pushdown

当前状态：

- `PatternBinder` 已经捕获 `MATCH ... WHERE ...`。
- `MatchSpecBuilder` 会把它复制到 `Graph::MatchSpec::where_clause`。
- `Graph::MatchStep` 当前会收到 `MatchSpec`，并直接创建占位 `Graph::MatchSource`。
- 默认 graph source 不产出 rows，也不消费 predicates。
- Planner 仍然会添加 `FilterStep` 来保持语义。

QueryTree 重构应该先分类 pushdown opportunities，不要承诺立即带来性能提升。

下表是 **Phase 6 之后** 的 pushdown classification 示例，只有在 graph element property access 语义模型和 graph storage runtime semantics 都落地后才能启用。Phase 3 的 `GQLPredicateClassificationPass` 第一版只应区分 trivially safe / unsafe；只要 predicate 依赖尚未定义清楚的 graph element property access，就保持为 post-source filter，不做 pushdown。

候选分类：

| Predicate | 分类 |
|-----------|----------------|
| `n.id = 1` | 单个 graph element binding 的 property equality；在 property access 语义落地后是 pushdown candidate。 |
| `e.weight > 10` | 单个 edge binding 的 property range；在 property access 语义落地后是 pushdown candidate。 |
| `n.id = m.id` | 通常是 post-source filter，除非 graph storage 支持 join-like predicate evaluation。 |
| aggregate predicates | post-aggregation filter。 |
| subquery predicates | post-source 或未来 apply-aware planning。 |

### 9.3 Apply

保留 `ApplyPlanner` 作为 row-correlated sources 的显式未来边界。QueryTree 设计应该让 correlation 在 analysis 中可见，但 `ApplyStep` 的实现可以留到后续阶段。

## 10. 实施阶段

### Phase 0：收紧设计文档

- 确认 `GQLLinearQueryNode` 作为 single-query QueryTree root。
- 从设计中移除 `QueryNode` inheritance / wrapping / JOIN slot alternatives。
- 修正 graph match mode 和 path mode 的术语混淆。
- 让示例对齐当前 parser AST field names 和 test layout。

### Phase 1：添加 QueryTree 节点骨架

文件：

```text
src/Analyzer/GQL/GQLQueryTreeNodes.h
src/Analyzer/GQL/GQLQueryTreeNodes.cpp
```

工作：

- 在 `src/Analyzer/IQueryTreeNode.h` 中添加 `QueryTreeNodeType` entries。
- 实现 `cloneImpl`、`dumpTreeImpl`、`isEqualImpl` 和 `updateTreeHashImpl`。
- 第一阶段只强制 `dumpTreeImpl` 能服务 `EXPLAIN QUERY TREE` / debug；`toASTImpl` 可以默认 throw `NOT_IMPLEMENTED`，等需要 `EXPLAIN AST` 或 QueryTree-to-AST round-trip 时再补。
- 为 node clone / dump / equality 添加聚焦单元测试。

### Phase 2：实现 AST 到 `GQL` QueryTree builder

文件：

```text
src/Analyzer/GQL/GQLQueryTreeBuilder.h
src/Analyzer/GQL/GQLQueryTreeBuilder.cpp
```

工作：

- Builder 只负责结构转换，不做 name resolution、type analysis 或 semantic validation。Parser AST 的字段应尽量原样映射到 QueryTree nodes；trivial enum / literal 转换可以做，但不要在 builder 里偷偷调用 `PatternBinder` 或做 scope 推导。
- 把 `GQLSingleQuery` 转成 `GQLLinearQueryNode`。
- 把 `GQLCombinedQuery` 转成 `GQLCombinedQueryNode`。
- 把已支持的 source 和 post-source clauses 转成 semantic steps。
- 保留当前 `MATCH` semantics：match mode、keep、yield、optional match、optional block、path prefix、path alternation、simplified path pattern、compound edge directions、labels、properties、predicates 和 quantifiers。
- 把 parser `GQLPageClause` 拆成可选 `GQLOrderByNode` 和可选 `GQLPageNode`；QueryTree 层不要继续让 `GQLPageNode` 拥有 `order_by`。
- builder 内不要 lower 到 `QueryPlan`。

### Phase 3：实现 analysis passes

文件：

```text
src/Analyzer/GQL/Passes/GQLNameResolutionPass.h
src/Analyzer/GQL/Passes/GQLNameResolutionPass.cpp
src/Analyzer/GQL/Passes/GQLTypeAnalysisPass.h
src/Analyzer/GQL/Passes/GQLTypeAnalysisPass.cpp
src/Analyzer/GQL/Passes/GQLPatternAnalysisPass.h
src/Analyzer/GQL/Passes/GQLPatternAnalysisPass.cpp
```

工作：

- 把可复用 binding logic 从 direct planning 移到 analysis。
- 在 source steps 和 projection steps 后推导 visible bindings。
- 保留 graph source predicates，供后续 storage pushdown 使用。
- 跟踪 active graph scope，避免 graph-qualified source scope 泄漏到 outer queries。

### Phase 4：构建 QueryTree-backed planner

可能文件：

```text
src/Interpreters/GQL/GQLQueryTreePlanner.h
src/Interpreters/GQL/GQLQueryTreePlanner.cpp
```

或者后续迁移到：

```text
src/Planner/GQL/GQLPlanner.h
src/Planner/GQL/GQLPlanner.cpp
```

工作：

- 对已支持 queries 生成与 direct planner 相同形状的 `QueryPlan`。
- 尽量复用现有 `MatchSpecBuilder` 和 expression / clause planning helpers。
- 保持 `Graph::MatchStep` 作为 graph source boundary。
- 如果迁移期间两条 planner paths 需要共存，添加 internal setting 或 compile-time switch。

### Phase 5：切换 `InterpreterGQLQuery`

工作：

- 添加四阶段链路：

```text
normalize AST
build GQL QueryTree
run GQL QueryTree passes
build QueryPlan
```

- 在 tests 证明等价前，保留 direct planner path。
- execution path 变化后，更新 `docs/graph/gql_runtime_flow.md`。

### Phase 6：predicate pushdown 和 graph storage 集成

工作：

- 只有在 analysis 有稳定 classification model 后，才给 `Graph::MatchSpec` 添加 pushdown predicate fields。
- 让真实 graph storage readers 消费这些 pushdown fields。
- 对未证明可安全下推的 predicates，保留 post-source `FilterStep`。

### Phase 7：apply 和 correlated graph sources

工作：

- 为 row-correlated nested graph sources 实现显式 apply semantics。
- 决定 `MatchStep` 是继续作为 `ApplyStep` 下面的 source，还是需要单独的 transforming graph operator。
- 不要把迁移到 `ITransformingStep` 当成 apply semantics 的替代品。

## 11. 验证计划

### 单元测试

优先添加贴近新代码的聚焦 gtests：

```text
src/Analyzer/GQL/tests/gtest_gql_query_tree_nodes.cpp
src/Analyzer/GQL/tests/gtest_gql_query_tree_builder.cpp
src/Analyzer/GQL/tests/gtest_gql_query_tree_analysis.cpp
src/Interpreters/tests/gtest_gql_interpreter.cpp
src/Parsers/graph/tests/gtest_gql_pattern_binder.cpp
```

测试矩阵：

- `MATCH (n) RETURN n`
- consecutive `MATCH` clauses
- `MATCH ... WHERE ...`
- node / edge labels and properties
- compound edge directions
- path variable
- path prefix
- path alternation
- edge quantifier
- `MATCH ... YIELD ...`
- `KEEP`
- `OPTIONAL MATCH` fail-closed execution，但 tree / spec 要保留
- `OPTIONAL { MATCH ... }` / `OPTIONAL ( MATCH ... )` 作为 `GQLOptionalBlockNode` 保留，执行可 fail closed
- `USE g MATCH ...`
- source-free `RETURN`
- `ORDER BY ... OFFSET ... LIMIT ...` 拆成 `GQLOrderByNode` + `GQLPageNode`
- 当前已支持形状下的 `LET`、`FOR`、`CALL`、`FINISH`
- `UNION ALL`, `UNION DISTINCT`, `EXCEPT`, `INTERSECT`

### Stateless tests

添加 stateless query tests 时，遵守仓库编号规则：

```bash
ls tests/queries/0_stateless/[0-9]*.reference | tail -n 1
```

然后递增最后一个 prefix。当前 `GQL` tests 已经在 `04034` 到 `04054` 附近，所以示例不应该再使用 `03500`。

每个 stateless `GQL` test 都应该包含当前 dialect setup：

```sql
SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
```

在 graph storage 实现前，避免添加依赖真实 graph values 的测试。例如，如果 `n` 仍然只是 placeholder `UInt64` binding，而不是 graph element value，那么 `MATCH (n:Person) RETURN n.name` 可能还太早。

### Integration tests

在 `tests/integration/test_gql/` 目录存在前，不要引用它。如果后续 graph storage integration 引入 server-level behavior，再按 ClickHouse integration-test conventions 新增 integration test 目录。

### Performance tests

在真实 graph reader 消费 pushdown predicates 前，不要声称性能提升。在此之前，有价值的验证是 plan equivalence 和 predicate classification correctness，而不是 benchmark speedup。

## 12. Reviewer checklist

实现开始前，先 review 这些问题：

1. `GQLLinearQueryNode` 是否保留 ordered semantic steps，而不是复制 grammar noise？
2. `GraphMatchMode` 和 path mode / search prefix 是否分开建模？
3. path variables 和 path prefixes 是否分离？
4. path alternation 表示的是否是 path alternatives，而不是 label alternatives？
5. `KEEP` 是否保留 path-selection AST，而不是 variable list？
6. 当前所有 `MatchSpec` fields 是否都能表达？
7. `MATCH ... WHERE ...` 是否仍附着在 match node 上，并且仍能作为 post-source filter 执行？
8. ClickHouse expression nodes 是否只在语义匹配时复用？
9. `OPTIONAL { ... }` / `OPTIONAL ( ... )` 是否建成 block-level node，而不是塞进 `GQLMatchNode`？
10. `GQLPageNode` 是否只保存 `OFFSET` / `LIMIT`，所有排序是否都走 `GQLOrderByNode`？
11. 设计是否明确 `PatternBinder` 的职责最终迁移到 `GQLPatternAnalysisPass`，并且 planner 只消费 analyzed tree？
12. SQL `QueryNode` 是否保持不动？
13. QueryTree-backed planner 是否能在删除旧 helpers 前，生成与当前 direct planner 相同形状的 `QueryPlan`？

## 13. 当前建议

采用 QueryTree 风格，但把它做成 sibling extension：

```text
IQueryTreeNode
  SQL nodes:
    QueryNode
    UnionNode
    JoinNode
  GQL nodes:
    GQLLinearQueryNode
    GQLCombinedQueryNode
    GQLMatchNode
    GQLPathPatternNode
    ...
  shared expression nodes:
    IdentifierNode
    ColumnNode
    ConstantNode
    FunctionNode
    SortNode
    ListNode
```

不要从 `QueryNode` 上移除 `final`。不要把 `MATCH` 存进 `QueryNode::getJoinTree`。不要为了复用固定 SQL 槽位而包一层 SQL `QueryNode`。应该构建一套大语义节点匹配 `GQL` 执行模型的 `GQL` QueryTree，再在语义真正共享的位置选择性复用 ClickHouse expression nodes 和 QueryPlan steps。
