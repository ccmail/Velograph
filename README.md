# LeoGraph

LeoGraph is a ClickHouse fork focused on adding analytical property-graph query
capabilities to ClickHouse.

The project goal is to let ClickHouse users query graph-shaped data with
standard `GQL` while continuing to use ClickHouse's native storage, execution,
and operational model. LeoGraph is not intended to become a standalone OLTP
graph database. It is aimed at analytical graph workloads: graph pattern
matching, multi-hop analysis, graph-shaped projections, and future graph
algorithms over data that already lives in ClickHouse tables.

## Current Status

The active development focus is the `GQL` parser and normalized graph AST.

Implemented or actively maintained:

- `Dialect::gql` routing through `ParserGQLQuery`.
- `GQLParserUtils::parseStatement` as the production parser entry.
- ANTLR4-generated parser sources from `src/Parsers/graph/grammar/GQL.g4`.
- A ClickHouse-native `IAST` graph AST under `src/Parsers/graph/AST`.
- `GQLParseTreeVisitor` split by query, projection, pattern, expression, DML,
  DDL, and type translation units.
- Parser contract tests in `src/Parsers/graph/tests/gtest_gql_parser.cpp`.

Not implemented yet:

- Runtime interpretation and lowering of `GQL*` AST nodes into a ClickHouse
  `QueryPlan`.
- Persistent graph catalog execution.
- Graph-specific execution operators such as expand and multi-hop traversal.
- Full `OpenGQL` session and transaction command support.

In the current parser-only phase, a syntactically supported `GQL` statement can
parse successfully and still fail later with `UNKNOWN_TYPE_OF_QUERY`, because no
graph interpreter is registered yet. Parser gaps fail earlier with `SYNTAX_ERROR`
or an explicit `Unsupported GQL ...` exception.

## Documentation

Start here:

| Document | Purpose |
|----------|---------|
| [Graph overview](docs/graph/index.md) | Project goals, current status, and document map |
| [Architecture](docs/graph/architecture.md) | Current parser architecture and target execution architecture |
| [GQL parser design](docs/graph/parser.md) | ANTLR4 integration, AST contract, and parser pipeline |
| [MATCH execution overview](docs/graph/match_execution/00_overview.md) | Authoritative MATCH execution architecture and design decisions |
| [Milestones](docs/graph/match_execution/06_milestones.md) | M0–M5 milestones and acceptance criteria |
| [Graph catalog design](docs/graph/catalog.md) | Target property graph catalog and table mapping design |
| [Plan expansion & operators](docs/graph/match_execution/03_plan_expansion.md) | Physical graph operators and expand-based execution model |

## Repository Layout

Important LeoGraph paths:

```text
docs/graph/                         Project and design documentation
src/Parsers/graph/                  GQL parser integration
src/Parsers/graph/AST/              ClickHouse-native GQL AST nodes
src/Parsers/graph/visitor/          ANTLR parse-tree to AST visitor
src/Parsers/graph/grammar/GQL.g4    Local OpenGQL grammar copy
src/Parsers/graph/generated/        Generated ANTLR4 C++ sources
src/Parsers/graph/tests/            Parser contract tests
tests/graph/                        Graph query samples and parser fixtures
```

## Upstream ClickHouse

LeoGraph is based on [ClickHouse](https://clickhouse.com/), an open-source
column-oriented database management system for real-time analytical workloads.
The upstream project is licensed under the
[Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0).

Useful upstream links:

- [ClickHouse documentation](https://clickhouse.com/docs/)
- [ClickHouse source repository](https://github.com/ClickHouse/ClickHouse)
- [ClickHouse website](https://clickhouse.com/)
