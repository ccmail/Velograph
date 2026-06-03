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

The project is currently transitioning from parser-first work into the first
interpreter / planner path. The main stable contract is still `GQL text ->
normalized GQL IAST`, and supported query roots now enter an initial
`InterpreterGQLQuery` / `GQL::GQLPlanBuilder` direct planner path. Graph catalog
execution and real graph storage integration are still future work.

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
- Add a new storage engine in the current parser phase.
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
| Interpreter / planner | Active MVP | `GQLSingleQuery` and `GQLCombinedQuery` enter `InterpreterGQLQuery`; reusable helpers under `src/Interpreters/GQL` plan supported source and post-source clauses while unsupported shapes fail closed. |
| Graph catalog execution | Design only | `catalog.md` describes the target table-mapping model. |
| Graph operators | Initial boundary | `Graph::MatchStep` and `Graph::MatchSource` define the current source contract; real expand / traversal operators remain design work. |

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
| [GQL parser design](parser.md) | Current parser architecture, AST contract, supported syntax, and dispatch rules. |
| [Interpreter readiness checklist](gql_ast_interpreter_todo.md) | Stable AST surface and fail-closed rules for future planner work. |
| [GQL runtime flow](gql_runtime_flow.md) | Code-reading guide from `executeQuery` through parser, runtime flow, current planner mapping, `MatchStep`, and `MatchSource`. |
| [Architecture](architecture.md) | Current implementation layers and target runtime architecture. |
| [Roadmap](roadmap.md) | Milestones, current parser work, and next implementation slices. |
| [Graph catalog design](catalog.md) | Future property graph catalog and table mapping model. |
| [Graph operators design](operators.md) | Future expand and multi-hop execution design. |
| [Grammar notes](../../src/Parsers/graph/grammar/README.md) | Local grammar changes and generation workflow. |

Historical parser notes live in [development/parser/README.md](development/parser/README.md).
They are kept for context only; the parser design document and status roadmap are
the authoritative development references.
