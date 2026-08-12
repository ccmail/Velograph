---
description: 'LeoGraph architecture: current parser implementation and target graph execution layers'
sidebar_label: 'Architecture'
sidebar_position: 81
slug: /development/graph/architecture
title: 'LeoGraph Architecture'
doc_type: 'reference'
---

# LeoGraph Architecture

LeoGraph is being developed in layers. The current implemented layers are the
`GQL` parser / AST contract, the QueryTree-based analyzer / planner path, and a
`MergeTree`-backed storage foundation. Catalog execution, indexed graph
lookups, and multi-element graph-specific query-plan operators remain target
architecture.

## Current Implementation: Parser and AST

The current parser pipeline is:

```text
GQL query text
  -> explicit Dialect::gql dispatch
  -> ParserGQLQuery
  -> GQLParserUtils::parseStatement
  -> ANTLR4 gqlStatement -> statement EOF
  -> GQLParseTreeVisitor
  -> GQL* IAST
```

Important properties of the current pipeline:

- `GQL` text is parsed only when the session or caller selects `Dialect::gql`.
- Ordinary ClickHouse `ParserQuery` does not sniff graph-looking prefixes such as
  `MATCH`, `USE`, or graph-shaped `SELECT`.
- `ParserGQLQuery` is a small dialect-specific parser wrapper, not an
  `IParserBase` implementation.
- The parser bypasses ClickHouse SQL token splitting and sends the complete
  caller-provided `GQL` span to ANTLR.
- Supported `GQLSingleQuery` and `GQLCombinedQuery` roots enter
  `InterpreterGQLQueryAnalyzer` (QueryTree + analysis passes + planner);
  unsupported runtime shapes still fail closed with explicit unsupported
  exceptions. The older `InterpreterGQLQuery` direct planner path is frozen
  legacy pending removal.

## Source Layout

Current parser-related code lives under lowercase `src/Parsers/graph`:

```text
src/Parsers/graph/
  ParserGQLQuery.h
  ParserGQLQuery.cpp
  GQLParserUtils.h
  GQLParserUtils.cpp
  GraphAST.h
  fwd_decl.h

  grammar/
    GQL.g4
    README.md
    generate.sh

  generated/
    GQLLexer.*
    GQLParser.*
    GQLVisitor.*
    GQLBaseVisitor.*

  AST/
    GQLSingleQuery.h
    GQLCombinedQuery.h
    GQLSubquery.h
    GQLCatalogStatement.h
    ...

  visitor/
    GQLParseTreeVisitor.h
    GQLParseTreeVisitorQuery.cpp
    GQLParseTreeVisitorProjection.cpp
    GQLParseTreeVisitorPattern.cpp
    GQLParseTreeVisitorExpression.cpp
    GQLParseTreeVisitorDML.cpp
    GQLParseTreeVisitorDDL.cpp
    GQLParseTreeVisitorType.cpp

  tests/
    gtest_gql_parser.cpp
```

Older documentation and early branches may mention `src/Parsers/Graph`,
`ParserGraphQuery`, `GQLParsingUtil`, or `ASTGraphQuery`. Those names belong to
the historical implementation shape and should not be used for new work.

## AST Contract

The parser produces ClickHouse-native AST nodes. Graph-specific nodes inherit
from `IAST` or `ASTWithAlias`; they do not use the earlier `INode`
ownership model.

The stable public root shapes are:

| Root | Purpose |
|------|---------|
| `GQLSingleQuery` | Linear query and DML clause sequences. |
| `GQLCombinedQuery` | Set queries such as `UNION`, `UNION ALL`, and `EXCEPT`. |
| `GQLSubquery` | Nested procedure bodies and `VALUE { ... }` style subqueries. |
| `GQLCatalogStatement` | Catalog DDL such as `CREATE` / `DROP` schema, graph, or graph type statements. |

AST ownership rules:

- `IAST::children` must remain dense and non-null.
- Optional graph children should live in named fields and enter `children` only
  when present.
- `clone` must deep-copy owned children.
- `formatAST -> parse -> formatAST` should preserve normalized output for
  supported shapes.

## Target Runtime Architecture

The intended end-to-end architecture is:

```text
GQL query text
  -> ParserGQLQuery
  -> GQL* IAST
  -> Graph interpreter / analyzer
  -> Graph catalog lookup and table mapping
  -> ClickHouse QueryPlan with graph-specific steps
  -> ClickHouse QueryPipeline
  -> MergeTree-backed vertex and edge tables
```

## Current Storage Foundation

`GraphStorageEngine` is registered as `GraphStorage` and manages four internal
objects inside the graph database:

```text
_graph
  -> vertices          MergeTree by __ID__
  -> edges_forward     ReplacingMergeTree by __SRC__, type, __RANK__
  -> edges_reverse     ReplacingMergeTree by __DST__, type, __RANK__
  -> vertex_degrees    materialized SummingMergeTree view
```

The storage interface deliberately exposes physical primitives instead of a
single high-level `MATCH` call. The query engine composes `scan`, `getVertex`,
`getEdge`, and `getNeighbors`; the storage chooses tables, projections, and
eventually key conditions.

The current executable storage path is a projected full scan through
`MergeTreeDataSelectExecutor::read`. Lookup primitives still ignore their key
or source-id inputs, and schema registration is in-memory only. See
[Graph storage foundation](storage_engine.md) for the precise support boundary
and follow-up list.

The future runtime layers are:

| Layer | Target Responsibility | Current State |
|-------|-----------------------|---------------|
| Interpreter / analyzer | Resolve graph names, validate AST, bind graph variables, and choose planning strategy. | Active: `InterpreterGQLQueryAnalyzer` + GQL QueryTree passes; predicate normalization and property resolution are M2 in [match_execution/](match_execution/00_overview.md). |
| Graph catalog | Store property graph definitions and map labels / properties to ClickHouse tables and columns. | Design only; the current in-memory registry is not a catalog. |
| Physical graph storage | Serve projected scans and indexed traversal primitives from ClickHouse tables. | Full-scan foundation implemented; lookup filtering, persistence, and optimization remain. See [Graph storage foundation](storage_engine.md). |
| Query-plan operators | Represent scans, expand steps, and vertex lookup; participate in QueryPlan optimizations. | M1 single-vertex lowering is implemented with logical `MatchStep` and physical `MatchVertexStep`; expand and lookup remain M4. |
| Pipeline processors | Execute expand and lookup operations while reusing ClickHouse processors where possible. | `MatchVertexStep` initializes the projected vertex source; driven expand / lookup processors remain M4. |

## Target Execution Model

For graph pattern execution, the model is expand-based rather than a pure
SQL-join rewrite: the planner emits one logical `MatchStep`, a plan-expansion
optimization decomposes it into physical operators (`MatchVertexStep`,
`MatchExpandStep`, `MatchVertexLookupStep`, ...), and standard plus
graph-specific QueryPlan optimizations bind `WHERE` conjuncts to those
operators so predicates are evaluated during the `MergeTree` scan itself.

The authoritative design, operator contracts, and milestones live in
[match_execution/](match_execution/00_overview.md).

## Integration Boundaries

Current integration points:

- `Dialect::gql` in ClickHouse settings and dispatch.
- `ParserGQLQuery` branches in server, client, and local connection parsing.
- ANTLR4 runtime reuse through the existing ClickHouse contrib infrastructure.
- Parser contract tests under `src/Parsers/graph/tests`.
- `InterpreterGQLQueryAnalyzer` registration, GQL QueryTree construction, and
  the M1 logical-to-physical `MatchStep` lowering path.
- `GraphStorage` registration in `StorageFactory` and resolution through
  `DatabaseCatalog` as `<graph_database>._graph`.
- Native reads from internal `MergeTree` tables through the `IGraphStorage`
  physical primitive contract.

Future integration points:

- Catalog metadata persistence and introspection.
- M2 property resolution and predicate normalization.
- M3 storage key-condition / prewhere pushdown and schema recovery.
- M4 driven expand / lookup processors and direction-complete execution.
- Runtime settings for graph traversal limits and resource controls.

## Development Rule

Parser work remains parser-only: do not add semantic catalog checks, storage
behavior, or execution workarounds inside the parser. Query branches compose
the `IGraphStorage` primitives, while storage branches own their physical
implementation and optimization. If a valid standard input cannot yet be
represented by the stable AST contract, keep an explicit
`Unsupported GQL ...` exception and track the gap with a concrete input and
expected AST shape.
