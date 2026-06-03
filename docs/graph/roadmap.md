---
description: 'Current LeoGraph implementation roadmap'
sidebar_label: 'Roadmap'
sidebar_position: 85
slug: /development/graph/roadmap
title: 'LeoGraph Roadmap'
doc_type: 'reference'
---

# LeoGraph Roadmap

This roadmap tracks the current `GQL` parser and interpreter planning work. It
replaces the older milestone list that described the initial `ASTGraphQuery`
prototype.

## Current Phase Summary

```text
P0: GQL parser / AST contract
    status: active and partially implemented

P1: Interpreter / planner MVP
    status: active and partially implemented

P2: Graph catalog execution
    status: target design

P3: Expand-based graph operators
    status: target design

P4: Multi-hop traversal and optimization
    status: future
```

The current priority is the handoff from `GQL text -> normalized GQL IAST` into
a reusable `GQL AST -> QueryPlan` framework. Runtime execution should consume
typed AST nodes and fail closed for unsupported shapes; it should not parse
`formatAST` output or infer semantics from raw source text.

## P0: GQL Parser and AST Contract

**Goal:** Parse complete caller-provided `GQL` statements under explicit
`Dialect::gql` and produce stable ClickHouse-native `GQL*` AST nodes.

### Completed or Active Work

| Area | Status |
|------|--------|
| Grammar import | `src/Parsers/graph/grammar/GQL.g4` is kept in-tree with local production-entry changes. |
| Generated parser | ANTLR4 C++ generated sources live in `src/Parsers/graph/generated`. |
| Parser entry | `ParserGQLQuery` routes the full input span through `GQLParserUtils::parseStatement`. |
| Dialect routing | `Dialect::gql` is selected explicitly; ordinary ClickHouse SQL parsing does not prefix-sniff `GQL`. |
| AST roots | `GQLSingleQuery`, `GQLCombinedQuery`, `GQLSubquery`, and `GQLCatalogStatement` form the stable root contract. |
| Visitor split | The parse-tree visitor is split across query, projection, pattern, expression, DML, DDL, and type files. |
| Query clauses | `MATCH`, `OPTIONAL MATCH`, `WHERE`, `FILTER`, `RETURN`, `SELECT`, `ORDER BY`, `OFFSET`, `LIMIT`, `USE`, `CALL`, `LET`, `FOR`, and `FINISH` have structured parser coverage for supported forms. |
| Patterns | Node / edge patterns, label expressions, path modes, path search prefixes, parenthesized paths, quantified path primaries, classic alternation, and simplified path expressions have structured AST coverage. |
| Expressions | `GQLExpr` and `GQLCaseExpr` cover the active expression subset, including functions, `CASE`, `CAST`, predicates, records, lists, temporal / duration literals, dynamic parameters, and path / value query wrappers. |
| DML / catalog DDL | Parser AST nodes exist for `INSERT`, `SET`, `REMOVE`, `DELETE`, and common catalog DDL forms. These are parser-only. |
| Tests | Parser contract tests live in `src/Parsers/graph/tests/gtest_gql_parser.cpp`. |

### Remaining P0 Work

1. Keep `ParserGQLQuery` and `GQLParserUtils::parseStatement` synchronized with
   documentation and focused tests.
2. Build a concrete `throwUnsupported` reproduction table. Each entry should
   include minimal input, grammar rule, current behavior, intended AST shape, and
   test coverage.
3. Finish thin reference-shape cleanup where interpreter work needs structured
   names for graph, graph type, binding table, procedure, and parameter
   references.
4. Broaden `SELECT FROM`, query-source, and projection coverage without changing
   public query root shapes.
5. Normalize DML and catalog visitor entry points for maintainability.
6. Keep extending the `formatAST -> parse -> formatAST` idempotence corpus.
7. Document and preserve grammar-generation reproducibility across macOS and
   Linux.

### P0 Acceptance Criteria

- Supported `GQL` inputs parse through `ParserGQLQuery` only under
  `Dialect::gql`.
- ClickHouse SQL mode rejects graph-looking `GQL` text instead of producing
  `GQL*` AST.
- Supported inputs have stable root kinds and dense non-null `children`.
- `clone` and normalized round-trip formatting are covered for newly added AST
  shapes.
- Unsupported grammar branches fail explicitly with clear parser exceptions.

## P1: Interpreter / Planning MVP

**Goal:** Plan a narrow, stable `GQL` query subset from typed AST nodes into a
ClickHouse execution path.

The recommended MVP boundary is:

```text
MATCH pattern
  -> optional WHERE predicate
  -> RETURN aliased items
  -> optional ORDER BY / OFFSET / LIMIT
```

Initial expression support should be whitelist-based:

- identifiers and property access;
- numeric, string, boolean, and `NULL` literals;
- arithmetic, comparison, and boolean operators;
- basic aggregates such as `COUNT`, `SUM`, `MIN`, `MAX`, and `AVG`;
- `CASE` and `CAST` only when the target planning representation is explicit.

P1 should not include DML, catalog DDL execution, graph type DDL, full procedure
calls, session commands, or speculative function mapping.

Use [GQL AST / Interpreter Readiness](gql_ast_interpreter_todo.md) as the
handoff checklist before implementing this phase.

## P2: Graph Catalog Execution

**Goal:** Introduce a metadata layer that maps property graph names, labels,
properties, and edge directions to existing ClickHouse tables and columns.

Target work:

1. Define persistent graph metadata structures.
2. Validate graph definitions against `DatabaseCatalog`.
3. Store and load graph metadata.
4. Add introspection paths for registered graphs, vertex mappings, and edge
   mappings.
5. Connect catalog lookup to interpreter/planner.

[Graph Catalog and Table Mapping](catalog.md) describes the target model. It is a
design document, not a statement of current runtime support.

## P3: Expand-Based Graph Operators

**Goal:** Add graph-aware query-plan and pipeline pieces for vertex scans, edge
expansion, destination lookup, and single-hop graph patterns.

Target work:

1. Design the first executable planning path for a single vertex scan.
2. Add an execution path for one-hop outgoing and incoming edge expansion.
3. Reuse ClickHouse filtering, projection, sorting, limiting, and aggregation
   steps where possible.
4. Define block schemas for graph variables and edge bindings.
5. Add explainable plan output for supported graph queries.

[Graph Operators](operators.md) describes the target expand execution model. The
current repository does not yet contain the runtime `GraphScanStep` or
`GraphExpandStep` implementation.

## P4: Multi-Hop Traversal and Optimization

**Goal:** Support variable-length path patterns and analytical graph traversal.

Target work:

- Iterative frontier expansion.
- Configurable traversal depth limits.
- Visited-set tracking.
- Path recording when path values are returned.
- Predicate pushdown into vertex and edge scans.
- Column pruning and expand-order selection.
- Benchmarks against equivalent ClickHouse SQL where possible.

## Deferred Standard Completeness

These areas are intentionally deferred until the parser and first planner path
are stable:

- Full `OpenGQL` `gqlProgram` support.
- `SESSION SET`, `SESSION RESET`, `SESSION CLOSE`, `START TRANSACTION`,
  `COMMIT`, and `ROLLBACK`.
- Semantic type validation inside the parser.
- Full graph type metadata validation.
- Runtime DML and catalog DDL execution.
- Distributed graph expansion.
- Graph algorithms such as PageRank or connected components.

## Testing Strategy

| Level | Location | Purpose |
|-------|----------|---------|
| Parser unit tests | `src/Parsers/graph/tests/gtest_gql_parser.cpp` | AST root contracts, dense children, clone behavior, and normalized round-trip formatting. |
| Grammar samples | `tests/graph/parser` and `tests/graph/ldbc` | Parser fixtures and benchmark-inspired inputs. |
| Future stateless tests | `tests/queries/0_stateless` | End-to-end parser and interpreter behavior once runtime support exists. |
| Future integration tests | `tests/integration` | Multi-node catalog and execution behavior once graph runtime exists. |

For parser-only smoke checks, use `clickhouse local` with
`--allow_experimental_gql_dialect=1 --dialect=gql`. Supported
`GQLSingleQuery` / `GQLCombinedQuery` inputs now enter `InterpreterGQLQuery`;
unsupported runtime shapes should fail closed with explicit unsupported
exceptions.
