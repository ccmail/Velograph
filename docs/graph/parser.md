---
description: 'GQL Parser design: ANTLR4 integration and Graph AST'
sidebar_label: 'GQL Parser'
sidebar_position: 82
slug: /development/graph/parser
title: 'GQL Parser Design'
doc_type: 'reference'
---

# GQL Parser Design

This document describes how the GQL (Graph Query Language, ISO/IEC 39075) parser is integrated into ClickHouse using ANTLR4.

## ANTLR4 Integration

### Existing Precedent: PromQL

ClickHouse already uses ANTLR4 for parsing PromQL (Prometheus Query Language):

```
contrib/antlr4-cpp-runtime/         -- ANTLR4 C++ runtime library
contrib/antlr4-cpp-runtime-cmake/   -- CMake build for the runtime
contrib/antlr4-grammars/            -- PromQL grammar (PromQL.g4)
contrib/antlr4-grammars-cmake/      -- CMake that generates C++ from .g4

src/Parsers/Prometheus/
  PrometheusQueryParsingUtil-antlr.cpp  -- ANTLR ParseTree -> ClickHouse AST
```

The GQL parser follows the exact same pattern.

### Grammar Source

The GQL grammar is sourced from the [opengql/grammar](https://github.com/opengql/grammar) repository, which provides a language-independent ANTLR grammar conforming to ISO GQL.

Key characteristics of the grammar:
- **Case-insensitive** keywords (`options { caseInsensitive = true; }`)
- **Combined** lexer and parser rules in a single `GQL.g4` file
- Covers the full GQL standard: `MATCH`, `RETURN`, `WHERE`, `INSERT`, `DELETE`, DDL, session management, transactions

### Build Pipeline

```
GQL.g4  ──(ANTLR4 tool)──>  GQLLexer.cpp / GQLParser.cpp / GQLVisitor.cpp
                                    |
                              (compiled into)
                                    |
                            ch_contrib::gql_grammar
                                    |
                              (linked by)
                                    |
                          src/Parsers/Graph/  (GQLParsingUtil.cpp)
```

CMake configuration:

```cmake
# contrib/gql-grammar-cmake/CMakeLists.txt
set(GQL_GRAMMAR_DIR "${ClickHouse_SOURCE_DIR}/contrib/gql-grammar")
set(GQL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT
        ${GQL_GENERATED_DIR}/GQLLexer.cpp
        ${GQL_GENERATED_DIR}/GQLParser.cpp
        ${GQL_GENERATED_DIR}/GQLBaseVisitor.cpp
    COMMAND ${ANTLR4_EXECUTABLE} -Dlanguage=Cpp -visitor -no-listener
            -o ${GQL_GENERATED_DIR}
            ${GQL_GRAMMAR_DIR}/GQL.g4
    DEPENDS ${GQL_GRAMMAR_DIR}/GQL.g4
)

add_library(ch_contrib::gql_grammar STATIC
    ${GQL_GENERATED_DIR}/GQLLexer.cpp
    ${GQL_GENERATED_DIR}/GQLParser.cpp
    ${GQL_GENERATED_DIR}/GQLBaseVisitor.cpp
)
target_link_libraries(ch_contrib::gql_grammar PUBLIC ch_contrib::antlr4_runtime)
```

## GQL Subset (Phase 1)

The initial implementation supports a core subset of GQL:

### Supported Statements

| Statement | Example | Priority |
|-----------|---------|----------|
| `MATCH ... RETURN` | `MATCH (a:Person) RETURN a.name` | P0 |
| `MATCH ... WHERE ... RETURN` | `MATCH (a)-[e]->(b) WHERE a.age > 25 RETURN b` | P0 |
| `CREATE PROPERTY GRAPH` | `CREATE PROPERTY GRAPH g VERTEX TABLES (...) EDGE TABLES (...)` | P1 |
| `DROP PROPERTY GRAPH` | `DROP PROPERTY GRAPH g` | P1 |
| `MATCH` with multi-hop | `MATCH (a)-[:E*1..5]->(b) RETURN b` | P3 |

### Supported Patterns

| Pattern | Syntax | Description |
|---------|--------|-------------|
| Node | `(a:Label)` | Vertex with optional variable and label |
| Directed edge | `-[e:Label]->` | Outgoing edge |
| Reverse edge | `<-[e:Label]-` | Incoming edge |
| Undirected edge | `~[e:Label]~` | Undirected edge |
| Multi-hop | `-[:Label*1..N]->` | Variable-length path |
| Property filter | `(a {name: 'Alice'})` | Inline property predicate |

### Deferred Features

- `INSERT` / `DELETE` / `SET` / `REMOVE` (graph DML)
- `USE graphExpression` (multi-graph)
- `SESSION` / `START TRANSACTION` (session management)
- `CALL` procedure
- Path modes (`WALK`, `TRAIL`, `SIMPLE`, `ACYCLIC`)
- `SHORTEST` path queries
- `OPTIONAL MATCH`

## Graph AST Design

### AST Class Hierarchy

```
IAST (ClickHouse base)
  |
  +-- ASTGraphQuery                   -- Top-level: MATCH ... RETURN ...
  |     children:
  |       [0] ASTGraphMatchClause     -- MATCH pattern list
  |       [1] ASTGraphWhereClause     -- WHERE (optional)
  |       [2] ASTGraphReturnClause    -- RETURN
  |
  +-- ASTGraphMatchClause             -- One or more graph patterns
  |     children:
  |       [0..N] ASTGraphPattern
  |
  +-- ASTGraphPattern                 -- A connected pattern: (a)-[e]->(b)
  |     children:
  |       [0..N] ASTNodePattern | ASTEdgePattern (alternating)
  |
  +-- ASTNodePattern                  -- (variable :Label {props})
  |     variable: String (optional)
  |     label: ASTLabelExpression (optional)
  |     properties: ASTGraphPropertySpec (optional)
  |
  +-- ASTEdgePattern                  -- -[variable :Label {props}]->
  |     variable: String (optional)
  |     label: ASTLabelExpression (optional)
  |     direction: Enum {LEFT, RIGHT, UNDIRECTED, ANY}
  |     quantifier: ASTPathQuantifier (optional, for *1..N)
  |
  +-- ASTPathQuantifier               -- *min..max
  |     min_hops: UInt64
  |     max_hops: UInt64 (0 = unbounded)
  |
  +-- ASTLabelExpression              -- Label with boolean operators
  |     type: Enum {NAME, AND, OR, NOT, WILDCARD}
  |     children: ASTLabelExpression[]
  |     name: String (for NAME type)
  |
  +-- ASTGraphReturnClause            -- RETURN item1, item2, ...
  |     distinct: bool
  |     children:
  |       [0..N] ASTGraphReturnItem
  |     order_by: ASTOrderByElement[] (optional)
  |     limit: ASTLiteral (optional)
  |
  +-- ASTGraphReturnItem              -- expression AS alias
  |     expression: ASTPtr
  |     alias: String (optional)
  |
  +-- ASTGraphWhereClause             -- WHERE condition
  |     children:
  |       [0] expression (reuse ClickHouse ASTFunction/ASTLiteral/etc.)
  |
  +-- ASTCreateGraphStatement         -- CREATE PROPERTY GRAPH
  |     graph_name: String
  |     if_not_exists: bool
  |     vertex_tables: ASTGraphVertexTableDef[]
  |     edge_tables: ASTGraphEdgeTableDef[]
  |
  +-- ASTGraphVertexTableDef          -- VERTEX TABLE mapping
  |     table_name: String
  |     key_column: String
  |     label: String
  |     properties: [(source_col, alias)]
  |
  +-- ASTGraphEdgeTableDef            -- EDGE TABLE mapping
        table_name: String
        key_column: String
        source_key: String
        source_ref_table: String
        source_ref_column: String
        dest_key: String
        dest_ref_table: String
        dest_ref_column: String
        label: String
        properties: [(source_col, alias)]
```

### Property Access

GQL uses dot notation for property access: `a.name`, `e.created_at`. These are translated to ClickHouse `ASTIdentifier` nodes with a qualified name that the interpreter resolves against the vertex/edge table schema.

```
GQL: a.name
  -> ASTFunction("graphPropertyAccess",
       ASTIdentifier("a"),        -- graph variable
       ASTLiteral("name"))        -- property name
```

During interpretation, `a` is resolved to its underlying table (e.g., `users`), and `name` is resolved to the corresponding column.

### Expression Reuse

WHERE conditions and RETURN expressions reuse ClickHouse's existing expression AST nodes (`ASTFunction`, `ASTLiteral`, `ASTIdentifier`) wherever possible. Only graph-specific constructs (patterns, labels, path quantifiers) use new AST types.

## Parser Pipeline

### Entry Point

```cpp
// src/Parsers/Graph/ParserGraphQuery.h
class ParserGraphQuery : public IParserBase
{
protected:
    const char * getName() const override { return "Graph query (GQL)"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected) override;
};
```

### Detection and Dispatch

```cpp
// ParserGraphQuery.cpp
bool ParserGraphQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    // Detect GQL entry keywords
    if (ParserKeyword(Keyword::MATCH).check(pos, expected))
    {
        // Backtrack and extract GQL text
        // Parse via ANTLR4
        // Convert to Graph AST
        return true;
    }

    if (ParserKeyword(Keyword::CREATE).check(pos, expected)
        && ParserKeyword(Keyword::PROPERTY).check(pos, expected)
        && ParserKeyword(Keyword::GRAPH).check(pos, expected))
    {
        // Parse CREATE PROPERTY GRAPH via ANTLR4
        return true;
    }

    return false;
}
```

### ANTLR4 Invocation

```cpp
// src/Parsers/Graph/GQLParsingUtil.h
class GQLParsingUtil
{
public:
    /// Parse a GQL MATCH query and return a Graph AST
    static ASTPtr parseMatchQuery(const std::string & gql_text);

    /// Parse a CREATE PROPERTY GRAPH statement
    static ASTPtr parseCreateGraph(const std::string & gql_text);

private:
    /// Convert ANTLR4 parse tree context to Graph AST node
    static ASTPtr visitMatchStatement(GQLParser::MatchStatementContext * ctx);
    static ASTPtr visitGraphPattern(GQLParser::GraphPatternContext * ctx);
    static ASTPtr visitNodePattern(GQLParser::NodePatternContext * ctx);
    static ASTPtr visitEdgePattern(GQLParser::EdgePatternContext * ctx);
    // ... more visitor methods
};
```

### Registration in ParserQuery

```cpp
// src/Parsers/ParserQuery.cpp (modification)
bool ParserQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    // ... existing parsers ...

    // GQL parser (before the final fallback)
    ParserGraphQuery graph_query_p;
    if (graph_query_p.parse(pos, node, expected))
        return true;

    // ... remaining parsers ...
}
```

## Error Handling

ANTLR4 parse errors are caught and converted to ClickHouse exceptions:

```cpp
class GQLErrorListener : public antlr4::BaseErrorListener
{
    void syntaxError(
        antlr4::Recognizer * recognizer,
        antlr4::Token * offending_symbol,
        size_t line,
        size_t position,
        const std::string & msg,
        std::exception_ptr e) override
    {
        throw Exception(
            ErrorCodes::SYNTAX_ERROR,
            "GQL syntax error at line {}, position {}: {}",
            line, position, msg);
    }
};
```

## Testing Strategy

GQL parser tests follow ClickHouse conventions:

1. **Unit tests**: `src/Parsers/Graph/tests/gtest_gql_parser.cpp` - test individual parse rules.
2. **SQL tests**: `tests/queries/0_stateless/` - end-to-end tests with `.sql` and `.reference` files.
3. **Negative tests**: Verify that malformed GQL produces clear error messages.
