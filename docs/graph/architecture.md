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
`GQL` parser / AST contract and an initial interpreter planner path for
supported query roots. Catalog execution, storage-backed graph scans, and full
graph-specific query-plan operators are still target architecture.

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
- Supported `GQLSingleQuery` and `GQLCombinedQuery` roots now enter
  `InterpreterGQLQuery`; unsupported runtime shapes still fail closed with
  explicit unsupported exceptions.

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
from `IAST` or `ASTWithAlias`; they do not use the earlier kgraph `INode`
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

The future runtime layers are:

| Layer | Target Responsibility | Current State |
|-------|-----------------------|---------------|
| Interpreter / analyzer | Resolve graph names, validate AST, bind graph variables, and choose planning strategy. | Not implemented. |
| Graph catalog | Store property graph definitions and map labels / properties to ClickHouse tables and columns. | Design only. |
| Query-plan operators | Represent scans, expand steps, multi-hop traversal, and vertex lookup. | Design only. |
| Pipeline processors | Execute expand and lookup operations while reusing ClickHouse processors where possible. | Design only. |

## Target Execution Model

For graph pattern execution, the planned model is expand-based rather than a
pure SQL-join rewrite:

```text
start vertex scan
  -> expand through edge table
  -> lookup destination vertices
  -> filter / aggregate / project with ClickHouse plan steps
```

This model is intended to support variable-length paths, frontier pruning,
visited-set tracking, and future graph algorithms. The design is documented in
[Graph operators](operators.md), but these operators are not yet implemented.

## Integration Boundaries

Current integration points:

- `Dialect::gql` in ClickHouse settings and dispatch.
- `ParserGQLQuery` branches in server, client, and local connection parsing.
- ANTLR4 runtime reuse through the existing ClickHouse contrib infrastructure.
- Parser contract tests under `src/Parsers/graph/tests`.

Future integration points:

- Interpreter registration for supported `GQL*` roots.
- Catalog metadata persistence and introspection.
- Query-plan step registration or construction for graph scans and expands.
- Runtime settings for graph traversal limits and resource controls.

## Development Rule

Parser work should continue to be parser-only until the interpreter boundary is
implemented. Do not add semantic catalog checks, storage behavior, or execution
workarounds inside the parser. If a valid standard input cannot yet be
represented by the stable AST contract, keep an explicit `Unsupported GQL ...`
exception and track the gap with a concrete input and expected AST shape.
