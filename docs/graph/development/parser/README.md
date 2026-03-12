# GQL Parser Development Log

This directory tracks the development progress of the GQL parser integration.

## Current Status

### Phase: P0 - ANTLR4 GQL Parser Integration

**Status**: Compilation and basic testing PASSED.

## Implementation Summary

### Files Created

| File | Description |
|------|-------------|
| `src/Parsers/Graph/grammar/GQL.g4` | GQL ANTLR4 grammar (from opengql/grammar, 3774 lines) |
| `src/Parsers/Graph/grammar/generate.sh` | Script to regenerate C++ from GQL.g4 |
| `src/Parsers/Graph/generated/` | Generated C++ Lexer/Parser (8 files, ~64K lines) |
| `src/Parsers/Graph/ASTGraphQuery.h` | Graph AST node classes |
| `src/Parsers/Graph/ASTGraphQuery.cpp` | AST formatting implementation |
| `src/Parsers/Graph/GQLParsingUtil.h` | GQL parsing utility interface |
| `src/Parsers/Graph/GQLParsingUtil.cpp` | ANTLR parse tree traversal implementation |
| `src/Parsers/Graph/ParserGraphQuery.h` | Parser entry point interface |
| `src/Parsers/Graph/ParserGraphQuery.cpp` | Parser entry point implementation |

### Files Modified

| File | Change |
|------|--------|
| `src/Parsers/CMakeLists.txt` | Build `_gql_grammar` library from generated sources, linked to `clickhouse_parsers` |
| `src/Parsers/ParserQuery.cpp` | Registered `ParserGraphQuery` as first parser option |
| `src/configure_config.cmake` | Added `USE_GQL_GRAMMAR` flag (checks `antlr4_cpp_runtime` target) |
| `src/Common/config.h.in` | Added `#cmakedefine01 USE_GQL_GRAMMAR` |

### Architecture Decisions

1. **Grammar**: Using the full GQL.g4 grammar as-is (not trimmed). The ANTLR parser handles all GQL syntax; we only implement visitor methods for the subset we need.

2. **Generation model**: Following PromQL precedent - generated C++ files are committed to the repo. The `generate.sh` script is for regeneration when the grammar changes.

3. **AST design**: Minimal AST classes for Phase 0:
   - `ASTGraphQuery` - top-level MATCH query
   - `ASTGraphPattern` - pattern chain (alternating nodes and edges)
   - `ASTNodePattern` - `(variable:Label)`
   - `ASTEdgePattern` - `-[variable:Label]->`
   - `ASTGraphReturnClause` - RETURN clause
   - `ASTGraphReturnItem` - individual return item

4. **Parser priority**: `ParserGraphQuery` is registered first in `ParserQuery::parseImpl` to intercept `MATCH` keyword before it reaches SQL parsers.

5. **Conditional compilation**: All ANTLR code is guarded by `#if USE_GQL_GRAMMAR`, following the `USE_ANTLR4_GRAMMARS` pattern.

## Supported GQL Subset (Phase 0)

```gql
-- Node scan
MATCH (a:Person) RETURN a

-- Single edge traversal
MATCH (a:Person)-[:FOLLOWS]->(b:Person) RETURN b.name

-- Multi-hop pattern
MATCH (a:Person)-[:FOLLOWS]->(b:Person)-[:WORKS_AT]->(c:Company) RETURN c.name

-- Reverse edge
MATCH (a:Person)<-[:FOLLOWS]-(b:Person) RETURN b
```

## Not Yet Supported

- WHERE clause parsing (AST slot exists, visitor not yet connected)
- RETURN expression parsing (only simple property access)
- CREATE PROPERTY GRAPH DDL
- Variable-length paths (`*1..N`)
- Label expressions with boolean operators
- OPTIONAL MATCH
- Aggregations in RETURN

## Known Issues

- [ ] Token reconstruction in `ParserGraphQuery.cpp` is naive (space-separated)
- [ ] Error position mapping from ANTLR to ClickHouse position is approximate
- [ ] Property access (`a.name`) uses `tupleElement` function which may not be ideal

## Build and Test Results

### Compilation
- Full `clickhouse` binary builds successfully with the GQL parser integrated.
- Generated ANTLR4 code compiles with `-w` flag to suppress warnings.
- Host code uses `#pragma clang diagnostic` to isolate ANTLR header warnings.

### Runtime Tests
```
$ clickhouse local --query "MATCH (n:Person)"
Code: 78. DB::Exception: Unknown type of query: GraphQuery. (UNKNOWN_TYPE_OF_QUERY)

$ clickhouse local --query "MATCH (a:Person)-[e:KNOWS]->(b:Person)"
Code: 78. DB::Exception: Unknown type of query: GraphQuery. (UNKNOWN_TYPE_OF_QUERY)

$ clickhouse local --query "SELECT 1 + 2 AS result"
3
```

- `UNKNOWN_TYPE_OF_QUERY` is expected -- parser works, but `InterpreterGraphQuery` is not yet implemented.
- Standard SQL queries continue to work correctly.

## Next Steps

1. Add WHERE clause visitor
2. Add RETURN expression parsing
3. Create unit tests
4. Implement `InterpreterGraphQuery` (Phase P1)
5. Connect to Graph Catalog and expand-based execution (Phase P2)
