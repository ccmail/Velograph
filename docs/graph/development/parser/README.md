# Historical GQL Parser Development Log

This directory keeps early parser-development notes for historical context.

The current parser architecture has moved on from the first prototype described
by the old log. Do not use this file as the authoritative source for new parser
work.

Current references:

- [GQL parser design](../../parser.md)
- [MATCH execution overview](../../match_execution/00_overview.md)
- [Milestones](../../match_execution/06_milestones.md)
- [Grammar generation notes](../../../../src/Parsers/graph/grammar/README.md)
- Repository-local live status: `.claude/gql_refactor_status.md`

## Historical Context

The early implementation experimented with:

- `ParserGraphQuery` prefix sniffing from ordinary ClickHouse SQL mode.
- `ASTGraphQuery` and related `ASTGraph*` node classes.
- A `GQLParsingUtil` visitor that produced a smaller graph AST subset.
- A `src/Parsers/Graph` directory layout.

Those names are historical. Current parser work uses:

- `Dialect::gql` selected explicitly by settings or caller context.
- `ParserGQLQuery` as the dialect-mode parser wrapper.
- `GQLParserUtils::parseStatement` as the production ANTLR entry.
- `GQLParseTreeVisitor` split across `src/Parsers/graph/visitor`.
- ClickHouse-native `GQL*` AST nodes under `src/Parsers/graph/AST`.
- Stable roots such as `GQLSingleQuery`, `GQLCombinedQuery`, `GQLSubquery`, and
  `GQLCatalogStatement`.

## Current Rule

For new development, treat this file as an archive only. Update
`docs/graph/parser.md`, `docs/graph/match_execution/06_milestones.md`, and
`.claude/gql_refactor_status.md` instead when parser behavior or status changes.
