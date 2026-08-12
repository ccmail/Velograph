---
description: 'LeoGraph: analytical graph queries on ClickHouse'
sidebar_label: 'LeoGraph Overview'
sidebar_position: 80
slug: /development/graph
title: 'LeoGraph'
doc_type: 'reference'
---

# LeoGraph

LeoGraph is an analytical property-graph query layer being built on top of
ClickHouse. It aims to support standard `GQL` over data stored in ordinary
ClickHouse tables, while reusing ClickHouse's `MergeTree` storage, vectorized
execution, distributed query infrastructure, settings, and resource tracking.

The project is currently transitioning from parser-first work into the
analyzer / planner / storage integration phase. The main stable contract is
still `GQL text -> normalized GQL IAST`; supported query roots enter the
QueryTree-based `InterpreterGQLQueryAnalyzer` path. A `MergeTree`-backed
`GraphStorage` development foundation provides projected full scans, while
keyed traversal, schema lifecycle, and production optimization remain. The
authoritative query design lives in
[match_execution/](match_execution/00_overview.md); the exact physical-storage
boundary lives in [Graph storage foundation](storage_engine.md).

## Goals

- Reuse ClickHouse storage instead of introducing a standalone graph store.
- Represent graph data through table mappings for vertex and edge tables.
- Parse standard `GQL` with an ANTLR4-based parser derived from the OpenGQL
  grammar.
- Build an explicit, ClickHouse-native `GQL*` AST that later interpreter and
  planner code can consume without reparsing source text.
- Target analytical graph workloads such as pattern matching, multi-hop
  traversal, graph-shaped aggregations, and graph algorithms.

## Non-Goals

- Replace transactional graph databases for OLTP graph workloads.
- Treat the current graph storage foundation as production-ready before its
  schema lifecycle, indexed lookups, and recovery semantics are complete.
- Infer graph semantics from formatted source text in later interpreter code.
- Route graph-looking input through ordinary ClickHouse SQL parsing. Production
  `GQL` parsing is selected explicitly through `Dialect::gql`.

## Current Status

| Area | Status | Notes |
|------|--------|-------|
| `GQL` grammar | Active | Local grammar lives at `src/Parsers/graph/grammar/GQL.g4`. |
| Parser entry | Implemented | `ParserGQLQuery` calls `GQLParserUtils::parseStatement`. |
| AST layer | Active | Graph nodes live under `src/Parsers/graph/AST` and inherit from `IAST` or `ASTWithAlias`. |
| Visitor | Active | `GQLParseTreeVisitor` is split by query, projection, pattern, expression, DML, DDL, and type handling. |
| Parser tests | Active | Contract tests live in `src/Parsers/graph/tests/gtest_gql_parser.cpp`. |
| Interpreter / planner | Active | `GQLSingleQuery` and `GQLCombinedQuery` enter `InterpreterGQLQueryAnalyzer` (QueryTree + passes + `GQLPlanner`); the older `InterpreterGQLQuery` direct planner path is frozen legacy pending removal. |
| Graph storage | Development foundation | `GraphStorageEngine` manages internal `MergeTree` tables and provides projected full scans. Lookup inputs, schema persistence, and pushdown are not implemented yet. |
| `MATCH` execution | M1 implemented | The analyzer resolves `<current_database>._graph`, logical `MatchStep` lowers to `MatchVertexStep`, and `MATCH (n) RETURN n` reads real vertex ids. Properties, predicates, and multi-element patterns remain M2-M4. |
| Graph catalog execution | Design only | `catalog.md` describes the target table-mapping model. |

## Parser-Only Contract

The current production parser path is:

```text
ParserGQLQuery
  -> GQLParserUtils::parseStatement
  -> gqlStatement
  -> statement EOF
  -> GQLParseTreeVisitor
  -> GQL* IAST
```

The stable query root shapes are:

- `GQLSingleQuery` for linear query and DML clause sequences.
- `GQLCombinedQuery` for set queries such as `UNION`, `UNION ALL`, and `EXCEPT`.
- `GQLSubquery` for nested procedure bodies.
- `GQLCatalogStatement` for catalog DDL.

Parser work should preserve these root shapes unless a later design explicitly
changes the interpreter contract.

## Document Map

| Document | Use It For |
|----------|------------|
| [MATCH execution design](match_execution/00_overview.md) | Authoritative architecture and milestones for `MATCH` planning, optimization, and storage pushdown. |
| [GQL parser design](parser.md) | Current parser architecture, AST contract, supported syntax, and dispatch rules. |
| [Architecture](architecture.md) | Current implementation layers and target runtime architecture. |
| [Graph storage foundation](storage_engine.md) | Current physical layout, primitive behavior, development boundary, and required correctness / performance follow-up. |
| [Graph catalog design](catalog.md) | Future property graph catalog and table mapping model. |
| [Grammar notes](../../src/Parsers/graph/grammar/README.md) | Local grammar changes and generation workflow. |

Superseded documents (`gql_runtime_flow.md`, `gql_ast_interpreter_todo.md`,
`gql_analyzer_refactoring_plan.md`, `roadmap.md`, `operators.md`) were removed
in favor of `match_execution/`; consult git history if the historical context
is needed.

Historical parser notes live in [development/parser/README.md](development/parser/README.md).
They are kept for context only; the parser design and `match_execution/`
documents are the authoritative development references.
