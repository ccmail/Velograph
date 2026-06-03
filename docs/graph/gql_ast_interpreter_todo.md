# `GQL` AST / Interpreter Readiness TODO

This document is the handoff checklist between the parser/AST work and
interpreter / planner work.

The parser contract is still `GQL text -> normalized GQL IAST`, but the current
development focus is now the first reusable `GQL AST -> QueryPlan` framework.
Interpreter work should consume only the stable AST subset listed here and
should explicitly reject unsupported parser or AST shapes instead of guessing
semantics from source text.

## Stable Parser / AST Contract

The current production parser path is:

```text
ParserGQLQuery
  -> GQLParserUtils::parseStatement
  -> gqlStatement
  -> statement EOF
  -> GQLParseTreeVisitor
```

The stable query root contract is:

- `GQLSingleQuery` for linear query / DML clause sequences.
- `GQLCombinedQuery` for set queries such as `UNION`, `UNION ALL`, and `EXCEPT`.
- `GQLSubquery` for nested procedure bodies.
- `GQLCatalogStatement` as the catalog DDL clause carried by `GQLSingleQuery`.

The parser no longer uses `ParserGraphQuery` prefix sniffing. Production GQL parsing must be selected explicitly through `Dialect::gql`.

## Currently Usable AST Surface

Interpreter MVP can reasonably start from this subset:

- Query roots: `GQLSingleQuery`, `GQLCombinedQuery`, `GQLSubquery`.
- Core query clauses: `GQLMatchClause`, `GQLWhereClause`, `GQLReturnClause`, `GQLSelectClause`, `GQLPageClause`, `GQLUseClause`, `GQLFinishClause`.
- Filter predicates are represented as `GQLWhereClause::Type::Filter`, while match / select predicates use `Type::Where` and aggregate predicates use `Type::Having`; planning reuses the same predicate path but does not collapse these clause kinds in AST.
- Projection item model: `GQLAliasedItem`, with aliases owned by the item wrapper instead of expression nodes.
- Pattern AST: `GQLGraphPatternBlock`, `GQLPathPattern`, `GQLPathTerm`, `GQLNodePattern`, `GQLEdgePattern`, `GQLLabelExpression`, `GQLPropertyMap`, `GQLPropertyItem`, `GQLKeepClause`, `GQLPathSearchPrefix`, simplified path nodes.
- Expression AST: current `GQLExpr` / `GQLCaseExpr` surface, including property access, arithmetic / boolean operators, `CASE`, `CAST`, list / record constructors, aggregate set quantifier, dynamic parameters, temporal / duration literals, `VALUE { ... }`, `LET ... IN ... END`, and `PATH[...]`.
- Type AST: `GQLTypeExpression`, `GQLGraphTypeSpecification`, `GQLElementTypeSpecification`.
- DML clauses: `GQLInsertClause`, `GQLSetClause`, `GQLRemoveClause`, `GQLDeleteClause`.
- Procedure clauses: `GQLCallNamedClause`, `GQLCallInlineClause`, `GQLYieldClause`.
- Inline `CALL` variable scopes can import expression-backed outer bindings; imports from current current plan columns still require a later correlated apply model.
- Thin reference wrappers: `GQLSchemaReference`, `GQLCatalogObjectName`, `GQLGraphExpression`, `GQLBindingTableExpression`.

Interpreter code should still dispatch by concrete AST node type and fail closed for unknown node classes.

## Current Interpreter / Planning Status

The current executable planner path is intentionally small but no longer
`MATCH`-only:

- `InterpreterGQLQuery` delegates `GQLSingleQuery` and `GQLCombinedQuery` root
  dispatch to `GQLPlanner`, so top-level queries and source subqueries can
  share the same root-plan builder.
- `GQL::GQLPlanBuilder` owns the mutable single-query planning state, while
  `ClauseSequencePlanner` owns linear clause-order dispatch. This mirrors the
  clause-query interpreter shape used by graph engines such as `kgraph` without
  registering every clause in ClickHouse's global `InterpreterFactory`.
- There is no interim planner-wide service bag. `GQL::PlanScope` only tracks
  lexical bindings and active graph scope. Graph-qualified source planning uses
  `PlanScope` graph-override helpers so source-local graph references do not
  leak into the outer query scope. Future graph source, catalog, or mutation
  services should enter through explicit contracts once their semantics are
  designed.
- `SourcePlanner` handles `MATCH`, source-free `RETURN` / `SELECT` / `LET` /
  `FOR` / `FINISH`, and `SELECT FROM` single sources. Source subqueries can use
  the same root planning as top-level queries, including supported combined
  query roots. Source-free clauses create an empty single-row source and then
  reuse the shared post-source `PostSourceClausePlanner` dispatch.
- `SourceCompositionPlanner` owns `SELECT FROM` source-list composition. It
  exposes reusable source-list entry classification, preserves the same-graph
  graph-match list path, and keeps different graph references / mixed source
  kinds behind explicit composition errors.
- `CallPlanner` owns position-aware `CALL` dispatch, inline `CALL`
  variable-scope handling, inline source / post-source entry points, and the
  named `CALL` fail-closed boundary. `SubqueryPlanner` owns shared subquery
  validation, binding definitions, and post-source subquery planning.
- `ApplyPlanner` is the dedicated boundary for row-correlated source clauses.
  It receives an explicit outer / subquery scope context and currently fails
  closed for nested source clauses that would require apply semantics.
- `MutationPlanner` and `CatalogPlanner` own fail-closed dispatch for
  data-modifying and catalog clauses. `GQLPlanBuilder` and post-source
  `SubqueryPlanner` route them to dedicated modules instead of letting generic
  `ClausePlanner` handle future catalog / storage semantics implicitly. They
  avoid routing `INSERT` / `SET` / `REMOVE` / `DELETE` / `CREATE` / `DROP`
  through generic source or post-source dispatch errors before real
  mutation/catalog execution exists.
- `ClausePlanner`, `AggregationPlanner`, and `ExpressionPlanner` provide the
  reusable post-source plan path for `WHERE`, `FILTER`, `HAVING`, projection,
  aggregation, `DISTINCT`, `ORDER BY`, `OFFSET`, `LIMIT`, `LET`, `FOR`, and
  `FINISH`.
- `PostSourceClausePlanner` is the shared dispatch point for clauses that appear after
  a source step exists. It routes service-aware boundaries such as catalog,
  DML, and post-source `CALL` before falling back to pure `ClausePlanner`.
- `ClausePlanner` treats upstream current plan columns as row values even when the
  header carries const metadata from source-free subqueries; projection-like
  steps materialize outputs before handing them to downstream clauses.
- `Graph::MatchStep` and `Graph::MatchSource` define the current graph-source
  boundary; the default source still emits no rows until graph storage is wired.

## Remaining Framework Gaps Toward Reusable Clause Planning

These are the main gaps before the current goal can be considered structurally
complete:

| Area | Current gap | Why it matters |
|------|-------------|----------------|
| source composition | `SourceCompositionPlanner` exposes source-list entry classification and can combine same-graph graph-match source lists into one `GraphMatch` source. Different graph references, mixed source kinds, and true multi-source composition still need explicit operator semantics, including cross/apply behavior, header conflict rules, and graph-scope restoration. | The composition boundary now has a dedicated module and reusable entry classifier, but it is not yet a complete source framework. |
| correlated subqueries | Post-source inline `CALL (x) { RETURN ... }` can reuse current row bindings when the nested body contains only post-source clauses. Inline `CALL` bodies that introduce a new source now fail through `ApplyPlanner`, with separate outer and subquery scopes passed through the apply context, because row-correlated apply semantics are not implemented. | Projection-like subqueries can compose with row data, and nested source failures have a single future implementation point with the required scope contract. Procedure bodies that need nested scans still require a real apply operator. |
| optional match execution | `OPTIONAL MATCH` and optional operand blocks are preserved in `MatchSpec` but rejected by execution. | Null-extension semantics require a real outer-match operator or source behavior. |
| real graph source | `Graph::MatchSource` currently uses the default empty reader path, and no graph catalog / table mapping is connected. A real graph source must be introduced through a concrete graph storage / planner contract rather than an interim service bag. | The plan shape is testable, but `MATCH` is not yet backed by storage. |
| DML and catalog execution | `GQLInsertClause`, `GQLSetClause`, `GQLRemoveClause`, `GQLDeleteClause`, and `GQLCatalogStatement` route to dedicated fail-closed planner boundaries from `GQLPlanBuilder` / `SubqueryPlanner`, but still have no runtime execution or service contract. | Future mutating and catalog statements now have explicit modules to implement instead of leaking through source / post-source dispatch. |
| expression breadth | Common scalar expressions lower through shared helpers, but temporal, duration, value-query, path-constructor, graph-expression, dynamic-parameter, and broader function semantics remain deferred. | Later clauses can reuse the helper layer, but only for the currently supported scalar subset. |
| named procedures | Inline `CALL` has source and post-source paths. Named `CALL` is routed through `CallPlanner` and fails closed with a dedicated exception, but procedure-reference binding and output-schema handling are not implemented. | Procedure calls need catalog/name resolution and output-schema handling. |

## Parser AST Gaps To Check During Interpreter Work

| Area | Current gap | Interpreter impact | Required behavior now | Parser follow-up |
|------|-------------|--------------------|-----------------------|------------------|
| `expression primary` | Some grammar alternatives still end in defensive `throwUnsupported` in `GQLParseTreeVisitorExpression.cpp`, especially `nonParenthesizedValueExpressionPrimary`, `objectExpressionPrimary`, and `valueExpressionPrimary`. | Some valid standard expressions may fail before interpreter receives AST. | Do not add interpreter workarounds for missing parser branches. If parsing fails, keep it a parser TODO. | Add support only from concrete failing input and add AST contract / round-trip test. |
| value-function families | Common numeric, string, datetime, duration, list, aggregate, and datetime-subtraction cases are represented, but family fallback guards remain. | Interpreter can plan supported `GQLExpr::Kind::FunctionCall` names but must reject unknown or unsupported names. | Keep function planning whitelist-based. Do not map unknown `GQL` function names to ClickHouse functions implicitly. | Expand one function family per slice from concrete input. |
| query shape | Some nested procedure bodies, focused linear query forms, primitive statement variants, and `SELECT` source combinations remain guarded. | Interpreter should only receive stable query roots for supported shapes. | Lower `GQLSingleQuery`, `GQLCombinedQuery`, and `GQLSubquery`; reject unknown clause order or unsupported clause node. | Keep root contract stable; only add parser shapes that fit existing roots. |
| references | Procedure references, parameter references, and some graph / binding-table references still contain thin or text-backed payloads. | Interpreter must not assume every reference is fully bound or catalog-resolved. | Treat reference AST as syntactic only. Perform catalog/name binding in analyzer or planner, with explicit unsupported cases for parameterized references if not implemented. | Finish shared reference helper for graph, graph type, binding table, and procedure names. |
| pattern edge cases | `complex optional match operand` and simplified direction unknown branches are still guardrails. | Most ordinary `MATCH` patterns are usable; uncommon optional-match shapes may fail before interpreter. | Lower only represented pattern nodes. Reject missing optional-match operand forms explicitly. | Add concrete tests before changing pattern AST shape. |
| ClickHouse-native AST reuse | `ExpressionPlanner` now handles `ASTIdentifier`, `ASTLiteral`, `ASTFunction`, and `ASTExpressionList` in the same planner path as `GQLExpr`; `WHERE` / `FILTER`, `ORDER BY`, `OFFSET`, `LIMIT`, aggregate detection, and `GROUP BY` identifiers consume expressions as `IAST`. Historical parser output still mostly uses `GQLExpr`. | Clauses can consume either current `GQLExpr` nodes or future semantically identical ClickHouse-native expression nodes. | Keep native-AST support behind shared expression / aggregation-planner helpers, not as separate clause paths. | Incrementally migrate only semantically identical SQL expression pieces. |
| type / graph type metadata | `GQLTypeExpression` and `GQLGraphTypeSpecification` are structured, but graph-type phrase forms, key-label metadata, and complex type normalization are not exhaustive. | Interpreter should not treat parser type AST as validated semantic type information. | Keep type compatibility and catalog validation outside parser. Reject unsupported type variants in analyzer or planner. | Harden type AST only from concrete inputs. |
| full standard program/session | Production entry is `gqlStatement`, not full `gqlProgram`. Session and transaction commands are out of current parser/AST scope. | Interpreter should not expect `SESSION SET`, `SESSION RESET`, `SESSION CLOSE`, `START TRANSACTION`, `COMMIT`, or `ROLLBACK` AST. | Keep these unsupported until product scope requires them. | Add AST nodes only if production entry expands beyond statement parsing. |

## Interpreter Self-Check Checklist

Before adding planner support for a new `GQL` feature, check:

1. Does the input parse through `ParserGQLQuery` under `Dialect::gql`?
2. Does the root stay within `GQLSingleQuery`, `GQLCombinedQuery`, `GQLSubquery`, or `GQLCatalogStatement`?
3. Does every consumed AST node have dense non-null `children` and a working `clone`?
4. Does `formatAST -> parse -> formatAST` preserve the same normalized text?
5. Does the planner code dispatch on typed AST fields rather than parsing `formatAST` output?
6. If the AST contains `GQLExpr::Kind::FunctionCall`, is the function name explicitly supported?
7. If the AST contains `GQLTypeExpression` or `GQLGraphTypeSpecification`, is the current layer only using syntactic type shape rather than assuming semantic validation?
8. If the AST contains `GQLGraphExpression`, `GQLBindingTableExpression`, or `GQLCatalogObjectName`, is name binding/catalog lookup handled in a dedicated analyzer or planner step?
9. If a node is unsupported, does the interpreter throw a clear `unsupported` exception instead of silently dropping it?
10. If planning needs graph source, catalog, or mutation services, is there an explicit service contract for that feature, and are those services kept out of `GQL::PlanScope`?

## Recommended Interpreter MVP Boundary

Start with a narrow planning slice:

```text
MATCH pattern
  -> optional WHERE predicate
  -> RETURN aliased items
  -> optional ORDER BY / OFFSET / LIMIT
```

Initial supported expression set should include:

- identifiers and property access;
- numeric, string, boolean, and `NULL` literals;
- arithmetic / comparison / boolean operators;
- `COUNT`, `SUM`, `MIN`, `MAX`, `AVG`;
- dynamic parameters only if parameter binding is already available;
- `CASE` and `CAST` only if target planning representation is explicit.

Do not include DML, catalog DDL, graph type DDL, or complex procedure calls in the first interpreter slice unless the planning contract for graph storage/catalog is already designed.

`FINISH` is planned as a terminal zero-column projection. It can close an existing source step such as `MATCH ... FINISH`, or start from the reusable empty single-row source for source-free forms such as `USE graph FINISH`.

## Parser Follow-Up Order

Parser work can continue in parallel with interpreter work in this order:

1. Build a concrete `throwUnsupported` reproduction table with minimal input, grammar rule, current behavior, and intended AST.
2. Finish reference-shape cleanup where interpreter needs structured names.
3. Fill `expression primary` gaps from concrete failing inputs.
4. Expand value-function families one family at a time.
5. Add query-shape coverage only when it preserves the existing root contract.
6. Harden graph/type AST metadata after interpreter has a real consumer.

## Non-Goals For Current Interpreter Work

- Do not implement full `OpenGQL` `gqlProgram` session / transaction commands.
- Do not infer graph catalog semantics from parser-only type AST.
- Do not use `ParserGraphQuery` or ClickHouse SQL prefix sniffing.
- Do not make parser workarounds inside interpreter for inputs that currently fail in `GQLParseTreeVisitor`.
- Do not plan unknown `GQL` functions by name similarity to ClickHouse SQL functions.
