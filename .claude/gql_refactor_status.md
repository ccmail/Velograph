# `GQL` Refactor Status

## Stable Anchor

- Branch: `parser/dev-gql-match-interpreter`
- Last stable checkpoint: see recent `[parser] ...` commits on the current branch.
- Checkpoint summary:
  - `GQLParserUtils` owns the `antlr4` entry, `SLL -> LL` fallback, and error listeners.
  - `GQLParseTreeVisitor` builds a minimal `IAST`-based `GQL*` graph AST.
  - Graph AST nodes live under `src/Parsers/graph/AST/`.
  - `ParserGQLQuery` is the main parser-only entry for `Dialect::gql`; it routes full statements through `GQLParserUtils::parseStatement`.
- Ordinary ClickHouse `ParserQuery` no longer routes graph-shaped prefixes into GQL; production GQL parsing requires explicit `Dialect::gql`.
- Interpreter work is now active on the same branch. The current objective is a reusable `GQL` lowering framework, not a `MATCH`-only prototype.

## Current Shape

### Parser pipeline

- Entry helpers live in `src/Parsers/graph/GQLParserUtils.cpp`.
- The current main dialect-mode root rule is `statement`, reached through `ParserGQLQuery` and `GQLParserUtils::parseStatement`.
- `parseStatement` covers query, DML, and catalog DDL AST construction; it is the only public ANTLR rule used by the GQL parser entry.
- The current normalization rule is: keep `visit*` boundaries aligned with `GQL.g4`, but keep public query roots aligned with a semantic query model instead of grammar wrappers.

### AST layer

- The new AST is intentionally kgraph-shaped but ClickHouse-native.
- All graph nodes inherit from `IAST` or `ASTWithAlias`, not kgraph `INode`.
- `IAST::children` must stay dense and non-null; optional graph children should stay in named fields and only be pushed into `children` when present.
- Query roots should stay stable:
  - linear clause queries return `GQLSingleQuery`;
  - set queries return `GQLCombinedQuery`;
  - `GQLCombinedQuery` stores `queries + operators`, and operators preserve `ALL` / `DISTINCT` semantics explicitly;
  - top-level `SELECT` returns a `GQLSingleQuery` whose first clause is `GQLSelectClause`, followed by `GQLPageClause` when paging is present;
  - `nestedQuerySpecification` returns `GQLSubquery`, and its inner `query` child is itself a normalized query root.

### Visitor coverage

The currently supported minimal path is:

- `MATCH`
- `OPTIONAL MATCH`
- `WHERE` / `FILTER`
- `RETURN` via `GQLReturnClause`
- `SELECT` via `GQLSelectClause`
- structured `GROUP BY` inside `RETURN` and `SELECT`
- `ORDER BY`
- `OFFSET`
- `LIMIT`
- structured `FINISH` clauses
- structured standalone `ORDER BY` / `OFFSET` / `LIMIT` paging clauses
- focused `USE` query parts flattened into clause sequences with structured `USE` clauses
- focused nested queries now preserve a structured subquery wrapper after the `USE` clause
- nested query specifications now preserve a structured `GQLSubquery` wrapper, with dedicated nodes for `AT schema`, `schemaReference`, binding-variable definition blocks, binding initializers, and `NEXT YIELD`, and reject unsupported inner non-query statements explicitly instead of flattening them to raw text
- top-level `SELECT` statements now normalize to `GQLSingleQuery`, with `GQLSelectClause` carrying `FROM` / `WHERE` / `GROUP BY` / `HAVING`, graph-qualified nested-query `FROM` sources preserved as structured source nodes, and `ORDER BY` / `OFFSET` / `LIMIT` emitted as a separate `GQLPageClause`
- the older `visitSelectStatement` exception on `SELECT ... FROM ...` paths is fixed by gathering parse-tree data before constructing the final clause node
- structured `CALL`, `LET`, and `FOR` clauses, including separate `GQLCallNamedClause` / `GQLCallInlineClause` nodes, structured procedure references, inline query-compatible `CALL { ... }`, inline `CALL (x, y) { ... }` variable scopes, `CALL ... YIELD`, typed `LET VALUE`, and `FOR ... WITH OFFSET` / `WITH ORDINALITY` fields
- `graphExpression` payloads in `USE`, graph-qualified `SELECT FROM`, and graph variable initializers now build `GQLGraphExpression` instead of top-level raw-text placeholders
- graph references, catalog object names, graph copy sources, and catalog DDL sources are structured as thin AST nodes where the grammar shape is clear
- `bindingTableExpression` payloads in binding-table initializers now build `GQLBindingTableExpression`, including the nested-query form
- structured `IS` truth checks and a first predicate subset such as `IS NULL`, `PROPERTY_EXISTS`, `ALL_DIFFERENT`, `SAME`, and source / destination predicates
- structured path / graph prefixes, `GQLCountSpec` for counted path-search prefixes, classic `pathPatternExpression` alternation via `GQLPathTerm` / `GQLPathPatternAlternation`, `MATCH ... YIELD`, optional `MATCH` blocks, parenthesized path patterns with quantifiers, simplified path-pattern expressions, `GQLQuantifiedPathPrimary` wrappers for quantified / questioned non-element path primaries, and basic path / node / edge patterns
- basic label expressions
- a minimal expression subset with structured `ABS` / length / cardinality functions, structured aggregate `DISTINCT` / `ALL` via `GQLExpr::SetQuantifier`, structured `COUNT(*)`, structured `ELEMENT_ID`, structured `EXISTS` operands, structured dynamic parameters / `SESSION_USER`, structured list / record literals, structured `DURATION_BETWEEN` with `TemporalQualifier`, and structured `TRIM` with `TrimSpec` + optional trim character
- simple set queries such as `UNION`, `UNION ALL`, and `EXCEPT`
- DML (`INSERT`, `SET`, `REMOVE`, `DELETE`) and catalog DDL (`CREATE` / `DROP` schema, graph, graph type) through `ParserGQLQuery` / `parseStatement`

### Parser-only AST reuse and raw text policy

- Use ClickHouse-native AST nodes for expression pieces whose semantics are identical to ClickHouse SQL, such as plain identifiers, literals, ordinary functions, and ordinary operators. Keep this behind graph visitor helper constructors so migration is incremental.
- Keep GQL-specific semantics in `GQL*` nodes: property access, graph / binding-table expressions, path / value queries, pattern trees, type predicates, `GQLTypeExpression`, and `GQLGraphTypeSpecification`.
- Typed declarations in `LET VALUE`, nested procedure binding definitions, `CAST`, and `IS TYPED` are structured with `GQLTypeExpression` instead of raw type strings.
- Nested graph-type specifications in catalog DDL are structured with `GQLGraphTypeSpecification` / `GQLElementTypeSpecification`; do not reintroduce `GQLCatalogStatement::source_text` for this path.
- Plain literal tokens may stay as parser leaves when no catalog, graph-reference, expression, or type structure is lost.
- New or changed AST nodes must keep `children` dense and non-null, deep-copy owned children in `clone`, and round-trip through normalized `formatAST`.

### Interpreter / lowering layer

- Production interpreter dispatch currently maps `GQLSingleQuery` and `GQLCombinedQuery` to `InterpreterGQLQuery`, with root plan construction shared through `RootLowering`.
- `InterpreterGQLQuery` and `GQL::PlanBuilder` both accept `GQL::PlanEnvironment`, giving catalog / storage resolution a single planner-wide dependency hook for graph scans and future non-scope services.
- `InterpreterGQLQuery` delegates query-root lowering to `RootLowering`; single-query clause lowering stays in `GQL::PlanBuilder`; clause-specific work should stay in reusable helpers under `src/Interpreters/GQL/`, not in one monolithic interpreter method.
- `PlanBuilder` separates source clauses from pipeline clauses. `MATCH` is a source boundary lowered through `SourceLowering` / `MatchLowering`; `SELECT FROM` source lists are classified through a reusable `SourceCompositionLowering` API; inline and named `CALL` entry points are isolated behind position-aware `CallLowering` dispatch; shared subquery validation, binding definitions, and pipeline-only subquery lowering live in `SubqueryLowering`; row-correlated source clauses are routed to `ApplyLowering` with an explicit outer / subquery scope context; DML and catalog clauses fail closed behind module-owned `MutationLowering` / `CatalogLowering` dispatch; `WHERE`, `RETURN`, `SELECT`, `ORDER BY`, `OFFSET`, `LIMIT`, `LET`, `FOR`, `FINISH`, `DISTINCT`, and aggregation are reusable pipeline/source helpers in `ClauseLowering` and `AggregationLowering`.
- `PlanScope` tracks only lexical state: current bindings and active graph scope. It also supports expression-backed bindings for nested procedure `VALUE` definitions, so a binding can be visible before it is physically present in the current plan header. Graph-qualified source lowering uses `PlanScope` graph-override helpers so source-local graph references do not leak into the outer query scope.
- `PlanEnvironment` carries planner-wide services such as `Graph::MatchSourceFactory`; nested `PlanBuilder` instances inherit the same environment without mixing storage dependencies into lexical scope.
- `ExpressionLowering` supports both current `GQLExpr` nodes and semantically identical ClickHouse-native `ASTIdentifier`, `ASTLiteral`, `ASTFunction`, and `ASTExpressionList` nodes through the same `ActionsDAG` path. `WHERE` / `FILTER`, `ORDER BY`, `OFFSET`, `LIMIT`, aggregate detection, and `GROUP BY` key extraction consume expressions as `IAST`, so filter, paging, and aggregation paths inherit the same expression contract.
- `ClauseLowering` builds pipeline `ActionsDAG` instances without duplicating const columns and materializes projection-like outputs, so source-free subquery constants remain row values when reused by outer `SELECT`, `WHERE`, and `ORDER BY`.
- `WHERE`, `FILTER`, and aggregation `HAVING` clauses keep distinct `GQLWhereClause::Type` values, while the interpreter reuses the common predicate-lowering path for all three.
- `USE graph`, graph-qualified `SELECT FROM`, nested subqueries, and inline `CALL { ... }` share graph-scope propagation through `PlanScope`.
- Inline `CALL (x) { ... }` can import expression-backed bindings from the outer `PlanScope`; imports that require row-by-row correlation remain explicit `NOT_IMPLEMENTED`.
- Consecutive `MATCH` clauses are lowered as one `GraphMatch` source with preserved per-clause specs. `MatchSpec` intentionally preserves graph reference, path constraints, label / property / predicate AST, `KEEP`, yield items, optional blocks, match mode, and path alternatives for future graph storage planning.
- `Graph::MatchStep` carries a `MatchSourceFactory` supplied through `InterpreterGQLQuery` / `PlanBuilder` / `PlanEnvironment`, so real graph storage can plug in a reader without changing query-clause lowering. The default factory still emits no rows until storage is wired.

### Current `throwUnsupported` coverage map

Track remaining `throwUnsupported` sites by parser-only impact, not by stale line numbers:

| Category | Current status | Priority |
|----------|----------------|----------|
| `predicate` | Existing grammar alternatives are structurally handled in `visitPredicateExprAlt`; the generic `predicate` fallback is a defensive guardrail. | Low until a concrete grammar branch reproduces. |
| `valueExpressionPrimary` / `nonParenthesizedValueExpressionPrimary` / `objectExpressionPrimary` | Current grammar alternatives are covered through `GQLExpr`, `GQLCaseExpr`, `GQLSubquery`, and path / let wrappers; remaining throws are defensive. | Low; do not churn without a failing input. |
| value-function families | Numeric, string, datetime, duration, list, aggregate, and datetime-subtraction branches have thin `GQLExpr` coverage; remaining throws are family guardrails. | Medium only when a new canonical input exposes a missed branch. |
| graph / binding / schema references | Most common graph references, catalog object names, graph sources, and binding-table references are thin structured AST; some grammar variants remain intentionally explicit unsupported. | Medium for obvious object-reference structure, otherwise defer. |
| query-shape guardrails | focused query, primitive query, `SELECT` shape, alias-on-non-aliasable, and complex optional-match branches protect the stable root contract. | Keep unless a stable `GQLSingleQuery` / `GQLCombinedQuery` / `GQLSubquery` shape is clear. |
| nested procedure metadata | `AT schema`, binding definitions, initializers, and `NEXT YIELD` are structured; unsupported initializer / non-query cases should stay explicit until represented. | Medium after expression gaps. |

The next implementation slice should start from a concrete parser input that currently throws, then add the minimum AST structure and contract test for that input. Do not delete a guardrail just to reduce the count.

### Phase 5: Expression completion

- `caseExpression` fully structured: `NULLIF` / `COALESCE` map to `GQLExpr::FunctionCall`, `CASE WHEN...END` (both simple and searched forms) use the new `GQLCaseExpr` AST node with `when_operands` / `then_results` / `else_result` fields; simple-CASE `caseOperand` and `whenOperand` now use `makeNpvepExpr` / `makeWhenOperandExpr` helpers for full expression dispatch; `compOp valueExpression` when-operands (e.g. `WHEN > 5`) structure as `GQLExpr::UnaryOp`; multi-operand `whenOperandList` (`WHEN 1, 2, 3`) uses `GQLExpr::ExprList` with individually structured children; `compOp` and predicate-part2 when-operands (IS NULL, IS DIRECTED, IS SOURCE OF, etc.) structured as `GQLExpr::BinaryOp` with cloned `caseOperand` as left child for complete predicate semantics, matching the existing full-predicate modeling pattern; `GQLCaseExpr::formatImplWithoutAlias` formats these BinaryOp when_operands as suffix-only (op + right) to preserve round-trip; `normalizedPredicatePart2` now fully structured as `BinaryOp` with `IS [NOT]` as operator and `[form] NORMALIZED` as literal right operand
- `castSpecification` structured: `CAST(operand AS type)` maps to `GQLExpr::Cast` with `children[0]` holding the operand and `text` holding the target type; round-trip formats as `CAST(x AS T)`
- `numericValueFunction` now covers all 13 grammar branches: the original 3 (`lengthExpression`, `cardinalityExpression`, `absoluteValueExpression`) plus `FLOOR`, `CEILING` / `CEIL`, `MOD`, `POWER`, `SQRT`, `SIN` / `COS` / `TAN` / `COT` / `SINH` / `COSH` / `TANH`, `LOG`, `LOG10`, `LN`, `EXP`
- `characterOrByteStringFunction` now covers all 5 branches structurally: `LEFT` / `RIGHT` (sub-character), `UPPER` / `LOWER` (fold), `BTRIM` / `LTRIM` / `RTRIM` (multi-trim), `NORMALIZE`, and `TRIM(trimOperands)` via `GQLExpr::TrimString` with explicit `TrimSpec` (Leading / Trailing / Both / None) and optional trim character child
- `makeValueFunction` refactored: split into `makeNumericValueFunction` + `makeCharacterOrByteStringFunction` for cleaner structure; `datetimeValueFunction`, `durationValueFunction`, `listValueFunction` structurally covered as `GQLExpr::FunctionCall` (Round C); `datetimeSubtraction` structurally covered as `GQLExpr::DurationBetween` with `TemporalQualifier` (Round D)
- `valueExpressionPrimary` now fully structured: `valueQueryExpression` maps to `GQLExpr::Kind::ValueQuery` with `GQLSubquery` child; `letValueExpression` maps to `GQLExpr::Kind::LetExpr` reusing `GQLAssignmentItem` bindings; `pathValueConstructor` maps to `GQLExpr::Kind::PathConstructor` with ordered node/edge children; `normalizedPredicateExprAlt` now has a dedicated `visitNormalizedPredicateExprAlt` override producing `BinaryOp` with `IS [NOT]` operator and `[form] NORMALIZED` literal right
- remaining `throwUnsupported(...)` sites should be tracked by category instead of stale line counts; the high-value parser-only categories are `predicate`, `valueExpressionPrimary`, `objectExpressionPrimary`, value-function families, and query-shape guardrails.

### Phase 6: DML statement support

- `linearDataModifyingStatement` now fully structured: the visitor handles both `focusedLinearDataModifyingStatement` and `ambientLinearDataModifyingStatement`, producing `GQLSingleQuery` with mixed query + DML clauses
- 4 new DML clause AST nodes: `GQLInsertClause` (with `GQLInsertPathPattern` for node-edge-node chains), `GQLSetClause` (with `GQLSetItem` for property / all-properties / label forms), `GQLRemoveClause` (with `GQLRemoveItem` for property / label), `GQLDeleteClause` (with `DetachMode` for `DETACH` / `NODETACH`)
- insert patterns reuse existing `GQLNodePattern` / `GQLEdgePattern` nodes, with `labelSetSpecification` built as `GQLLabelExpression::Conjunction`
- `callDataModifyingProcedureStatement` shares the same `makeCallClause` helper with `visitCallQueryStatement`, ensuring consistent AST output for both query-CALL and DML-CALL paths
- `buildSubquery` now handles `linearDataModifyingStatement` in nested procedure bodies (resolves the L1365 `nested non-query statement` gap for DML)
- catalog DDL is now represented by `GQLCatalogStatement` as a `GQLSingleQuery` clause; nested procedure bodies can carry catalog statements where the `GQLSingleQuery` contract permits it.

- `GQLParserUtils::parseStatement` is the main ANTLR entry for `ParserGQLQuery`, used for query, DML, and catalog DDL in explicit `Dialect::gql` mode; do not reattach heuristic GQL routing to ordinary `ParserQuery`.

### Simplified-path follow-up

- `getSimplifiedOverrideDirection` / `getSimplifiedOverrideSecondary` now cover all 7 `simplifiedDirectionOverride` grammar variants explicitly and fall back to `throwUnsupported(...)` instead of silently treating unknown branches as `EdgeDirection::Any`.
- `GQLSimplifiedPathExpr::clone()` now clears the copied `operands` vector before rebuilding cloned n-ary children, so the clone no longer carries source-child pointers in its intermediate state.
- `docs/graph/parser.md` now documents simplified-path whitespace normalization during round-trip formatting; `gtest_gql_parser.cpp` covers the 6 new shape cases for Any / Conjunction / Undirected / UndirectedOrRight / Group+Repetition / Clone n-ary plus a quoted-label round-trip test.

## Important Boundaries

- Top-level GQL parsing is dialect-selected, not prefix-selected. Ordinary ClickHouse `ParserQuery` must not produce `GQL*` AST nodes for graph-shaped input.
- Tests are useful as scratch coverage, but while the refactor is still long-lived they are not the primary progress gate.
- Parser exceptions should use existing ClickHouse codes such as `SYNTAX_ERROR`.
- Do not add a local fallback implementation of `__lsan_ignore_object`; use the shared declaration plus guarded call pattern.

## Recommended Next Steps

Current priority is `GQL AST -> reusable interpreter lowering -> QueryPlan` for a framework that later clauses can share. Parser-only changes should continue only when they unblock interpreter lowering or preserve the parser contract. Do not build new `MATCH`-only shortcuts; prefer small reusable slices in `PlanScope`, `SourceLowering`, `SourceCompositionLowering`, `CallLowering`, `SubqueryLowering`, `ClauseLowering`, `ExpressionLowering`, and `MatchLowering`.

Interpreter framework gaps to keep visible:

1. `Graph::MatchSource` has a factory / reader contract but no real graph storage implementation yet.
2. `OPTIONAL MATCH` and optional operand blocks are preserved in `MatchSpec` but rejected by execution.
3. Inline `CALL` variable scope imports are supported for standalone expression-backed bindings and for pipeline-only inline subqueries that reuse current row bindings. Inline `CALL` bodies that introduce a new source route through `ApplyLowering` with separated outer / subquery scopes and still throw `NOT_IMPLEMENTED`; subquery `AT schema`, binding-table / graph binding definitions, and `NEXT` statements are also explicit `NOT_IMPLEMENTED` paths. Empty inline `CALL () { ... }` scopes are accepted as no-import calls.
4. `SELECT FROM` source-list handling now has a dedicated `SourceCompositionLowering` module with a reusable entry-classification API. It can lower same-graph graph-match source lists into one `GraphMatch` source, while different graph references, mixed source kinds, and true multi-source composition still need real operator semantics.
5. Expression lowering still covers only the common scalar subset; temporal / duration / value-query / path-constructor / graph-expression execution lowering remains deferred.
6. `GQLCatalogStatement` and DML clauses have parser AST coverage and dedicated lowering boundaries. They are dispatched from `PlanBuilder` / pipeline-only `SubqueryLowering`, where `PlanEnvironment` is available for future catalog / storage services, but there is still no catalog / mutation execution.
7. `ClauseSequenceLowering` owns linear `GQLSingleQuery` clause-order dispatch. `PlanBuilder` now holds query-level state, while clause sequencing is a dedicated layer closer to the `kgraph` clause-query interpreter style.
8. `PipelineLowering` centralizes post-source clause dispatch so `ClauseSequenceLowering`, pipeline-only `SubqueryLowering`, and source-free query lowering share the same catalog / DML / pipeline `CALL` / pure pipeline-clause ordering after a source exists.
9. Full `ninja` verification is currently blocked by local build-directory regenerate issues under `build/contrib`; use direct TU compilation from `build/compile_commands.json` only as source-level isolation until the build directory is repaired.

## Open TODO Backlog

This section tracks the concrete places that are still incomplete or intentionally deferred in the current `GQL` parser-to-AST work. Use it as implementation direction for follow-up parser branches.

For interpreter / lowering handoff, use `docs/graph/gql_ast_interpreter_todo.md` as the current self-check document. It lists the stable AST surface, known parser gaps, and the fail-closed behavior expected from interpreter work.

### Parser-only v1 TODOs

1. Preserve the `ParserGQLQuery` driver contract in docs and tests.
   - `ParserGQLQuery` is not an `IParserBase` implementation and does not passthrough ClickHouse `SET`; it parses one complete caller-provided GQL span through `gqlStatement -> statement EOF`.
   - `allow_multi_statements` is currently ignored by the GQL driver because ClickHouse SQL token splitting is intentionally bypassed. If GQL multiquery support becomes necessary, design it explicitly instead of reintroducing ad-hoc semicolon splitting.
   - Keep tests for trailing semicolons, multi-statement rejection, explicit `Dialect::gql` dispatch, and ClickHouse-mode non-sniffing behavior.

2. Keep `ORDER BY ... NULLS FIRST/LAST` covered.
   - `GQLOrderByItem` now stores `NullOrdering` and `visitSortSpecification` preserves `NULLS FIRST` / `NULLS LAST`.
   - Keep round-trip and AST contract tests when adding new paging / ordering shapes.

3. Tighten the active `throwUnsupported(...)` map with executable examples.
   - The remaining guardrails in expression, reference, and query-shape visitors should stay until a concrete input reaches them.
   - Track each reachable guardrail with grammar rule, minimal input, current behavior, intended AST shape, and a positive or negative test before changing behavior.

4. Finish thin reference-shape cleanup where it affects round-trip structure.
   - `graphExpression` and `bindingTableExpression` already have thin AST wrappers, but procedure references and some catalog-object references still rely on direct `getText(...)` formatting.
   - Prefer a shared catalog-object-reference helper for graph, graph type, binding table, and procedure names, including parameter references and delimited identifiers.

5. Keep broadening source/projection coverage without changing query roots.
   - `SELECT FROM` already supports nested-query and graph-match sources, but more combinations should be covered by tests: multiple graph sources, qualified graph references, nested source query tails, aliases where grammar later permits them, and `HAVING` / paging combinations.
   - Keep the public root contract: linear queries are `GQLSingleQuery`, set queries are `GQLCombinedQuery`, nested procedure bodies are `GQLSubquery`.

6. Normalize DML / catalog visitor entry points for maintainability.
   - DML and catalog DDL currently work through higher-level helper functions from the statement entry, while many lower-level grammar contexts do not have dedicated visitor overrides.
   - Either add explicit overrides for `insertStatement`, `setStatement`, `removeStatement`, `deleteStatement`, and primitive catalog contexts, or document that these contexts are intentionally lowered only through the enclosing statement visitor.

7. Add parser contract tests for dialect routing edge cases.
   - Existing tests cover many `Dialect::gql` routes; keep extending that matrix when new grammar shapes are enabled.
   - Useful additions: semicolon handling, multi-statement rejection, `SKIP` normalization, qualified procedure calls, qualified binding-table references, and unsupported full-program commands.

8. Keep extending the `formatAST -> parse -> formatAST` idempotence corpus.
   - A small corpus now covers canonical query, pattern, expression, DML, and catalog DDL inputs.
   - Each new feature should keep asserting root kind, key fields, dense non-null `children`, `clone` deep-copy behavior, and normalized round-trip formatting.

9. Keep grammar generation reproducible across macOS and Linux.
   - Document the expected `ANTLR` tool version, the macOS `brew` path, the Linux jar path, and the generated-file check workflow.
   - Do not accept generated parser diffs unless they can be reproduced from `src/Parsers/graph/grammar/GQL.g4` and the documented generator path.

### Deferred Standard-Completeness TODOs

1. Decide whether production parsing should eventually accept full `gqlProgram`.
   - The current production entry is `gqlStatement -> statement EOF`; full `gqlProgram` with session and transaction activity is parsed only by the unused helper path.
   - If full standard coverage becomes a goal, add AST nodes and tests for `SESSION SET`, `SESSION RESET`, `SESSION CLOSE`, `START TRANSACTION`, `COMMIT`, and `ROLLBACK` before switching entry points.

2. Continue hardening structured type AST coverage.
   - `GQLTypeExpression` and `GQLGraphTypeSpecification` now replace the old raw type placeholders for typed declarations and nested graph-type catalog DDL.
   - Follow-up work should expand graph type phrase forms, key-label-set metadata, and complex type leaf normalization only from concrete parser inputs.

3. Define the parser / semantic-analysis boundary for type-validity checks.
   - The grammar intentionally accepts broad combinations for numeric, datetime, duration, string, list, path, and typed expressions.
   - Keep parser AST construction syntactic; move invalid type-combination checks to analyzer or a dedicated validation layer unless a syntax-only ambiguity requires parser action.

4. Extend grammar only from a concrete GQL-standard gap.
   - Do not add speculative grammar alternatives just to increase standard surface area.
   - When adding a standard feature that is not currently in `GQL.g4`, first add parser tests that show the desired input, AST shape, and normalized formatting.

Suggested order for the next implementation slices:

1. keep the `ParserGQLQuery` / `GQLParserUtils::parseStatement` entry contract synchronized between code, docs, and focused tests;
2. add `ORDER BY ... NULLS FIRST/LAST` support, because it is a concrete parsed-but-lost syntax feature;
3. start each visitor coverage slice from concrete GQL input that currently reaches `throwUnsupported(...)`, then add the minimum AST shape and focused contract test for that input;
4. keep `predicate`, `valueExpressionPrimary`, and value-function guardrails unless a real input proves a missing active grammar branch;
5. spend parser-only effort on query-shape and reference-shape gaps only when the target root can stay within `GQLSingleQuery`, `GQLCombinedQuery`, or `GQLSubquery`;
6. use `clickhouse local --allow_experimental_gql_dialect=1 --dialect=gql -q ...` as a quick smoke check: supported `GQLSingleQuery` / `GQLCombinedQuery` inputs now enter `InterpreterGQLQuery`, while parser gaps fail earlier with `SYNTAX_ERROR` or `Unsupported GQL ...`, and unsupported runtime shapes fail closed with `NOT_IMPLEMENTED`.

### 1. Keep the new query contract stable while filling coverage

Keep working in the split visitor implementation under `src/Parsers/graph/visitor/`, but do not widen the set of public query root shapes.

The immediate rule is:

- keep `GQLSingleQuery`, `GQLCombinedQuery`, and `GQLSubquery` as the intentional query-level wrappers;
- keep `GQLSelectClause`, `GQLReturnClause`, and `GQLPageClause` as separate clause nodes instead of collapsing them back into one projection node.

Then reduce only query-level `throwUnsupported` cases that have a concrete reproducing input and a stable AST root shape.

Suggested order:

- keep the remaining top-level graph-selection gaps focused on explicit `Dialect::gql` coverage
- widen nested query support beyond the single-statement unwrap path
- keep reducing the remaining query-level `throwUnsupported` branches before touching parser entry gating again; use richer path semantics beyond the current simplified path-pattern AST as a later pattern-side topic only when a failing parser-only input requires it
- introduce a thin `GQLQuery` base or equivalent shared query helper only if later lowering really benefits from it; do not churn the current roots just for naming

The goal of this phase is to make the current query-oriented root feel complete before adding more outer integration.

### 2. Continue filling the remaining pattern-side holes

After the main query chain is healthier, cover the remaining pattern-related unsupported branches:

- `KEEP ...` coverage beyond the current path-prefix wrapper
- richer path semantics beyond the current simplified path-pattern expressions, including any future lowering that needs more than the current wrapper/expression split
- broader optional-match block normalization if lowering wants something richer than the current block wrapper
- any missing path-search variants that round-trip tests expose

Most of this work should still stay in the relevant visitor translation unit, with small AST additions under `src/Parsers/graph/AST/` only when the current nodes stop being expressive enough.

### 3. Preserve the expression layer contract

Phase 5 + Rounds C/D/E/F covered most value-function and CASE branches. The three remaining `valueExpressionPrimary` gaps are now filled:

- `valueQueryExpression` (`VALUE { subquery }`) structured as `GQLExpr::Kind::ValueQuery` with `children[0]` = `GQLSubquery`
- `letValueExpression` (`LET ... IN ... END`) structured as `GQLExpr::Kind::LetExpr` with `children[0..n-2]` = `GQLAssignmentItem` bindings and `children[n-1]` = body expression
- `pathValueConstructor` (`PATH [ ... ]`) structured as `GQLExpr::Kind::PathConstructor` with ordered node/edge children
- `normalizedPredicatePart2` (`IS [NOT] [normalForm] NORMALIZED`) now fully structured as `BinaryOp` both in top-level `visitNormalizedPredicateExprAlt` and in CASE `whenOperand`, with `IS [NOT]` as operator and `[form] NORMALIZED` as literal right operand
- all simple-case predicate-part2 `whenOperand` forms (IS NULL / IS TYPED / IS DIRECTED / IS LABELED / IS SOURCE OF / IS DESTINATION OF / IS NORMALIZED) are now fully structured

Recommended direction:

- keep the generic `GQLExpr::Kind` approach; extend with dedicated nodes only when structure demands it (as done with `GQLCaseExpr`, `DurationBetween`, `TrimString`);
- postpone a large expression hierarchy until the parser coverage stabilizes.
- add contract tests before changing expression visitor guardrails, because the current active grammar branches are mostly covered and remaining throws are often defensive.

### 4. Revisit lowering later

Only after the visitor coverage is broad enough should the refactor move to:

- deciding how `GQL*` AST lowers into the long-term analyzer or execution layer;
- revisiting product-level GQL dispatch if requirements change beyond explicit `Dialect::gql`.

## Files To Touch Next

- `src/Parsers/graph/ParserGQLQuery.cpp`
- `src/Parsers/graph/ParserGQLQuery.h`
- `src/Parsers/graph/GQLParserUtils.cpp`
- `src/Parsers/graph/GQLParserUtils.h`
- `src/Parsers/graph/visitor/GQLParseTreeVisitor.h`
- `src/Parsers/graph/visitor/GQLParseTreeVisitor*.cpp`
- `src/Parsers/graph/AST/*.h`
- `src/Parsers/graph/fwd_decl.h`
- `src/Parsers/graph/GraphAST.h`
- `src/Parsers/graph/tests/gtest_gql_parser.cpp`
- `docs/graph/parser.md`
- `src/Parsers/graph/grammar/README.md`

## Session Recovery Note

If a future session starts cold, read this file first, then read `.claude/learnings.md`, and only then inspect the current `graph` parser sources.
