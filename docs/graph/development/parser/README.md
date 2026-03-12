# GQL Parser Development Log

This directory tracks the development progress of the GQL parser integration.

## Current Status

### Phase: P1 - Standard Visitor Pattern with Full GQL AST

**Status**: Compilation and basic testing PASSED.

**Refactoring**: Replaced `dynamic_cast` tree traversal with standard ANTLR4 Visitor pattern (following PromQL precedent). Extended AST node hierarchy to support complete GQL syntax.

## Implementation Summary

### Directory Structure

```
src/Parsers/Graph/
  grammar/
    GQL.g4                 -- Full GQL ANTLR4 grammar (opengql/grammar)
    generate.sh            -- C++ code generation script
  generated/
    GQLLexer.h/cpp         -- Generated lexer
    GQLParser.h/cpp        -- Generated parser (610 visitor methods)
    GQLVisitor.h/cpp       -- Generated visitor interface
    GQLBaseVisitor.h/cpp   -- Generated visitor base class
  ASTGraphQuery.h/cpp      -- Graph AST node classes (9 classes)
  GQLParsingUtil.h/cpp     -- GQLASTBuilder visitor implementation
  ParserGraphQuery.h/cpp   -- ClickHouse parser entry point
```

### AST Node Classes

| Class | Fields | Description |
|-------|--------|-------------|
| `ASTLabelExpression` | `op`, `label_name` | Label boolean expression tree (Name/And/Or/Not/Wildcard) |
| `ASTPathQuantifier` | `min_hops`, `max_hops` | Path quantifier: `*`, `+`, `?`, `{n}`, `{n,m}` |
| `ASTNodePattern` | `variable`, `label`, `label_expression`, `where_predicate` | Node pattern `(a:Person WHERE ...)` |
| `ASTEdgePattern` | `variable`, `label`, `direction`, `label_expression`, `quantifier`, `where_predicate` | Edge pattern `-[r:KNOWS*1..5]->` |
| `ASTPathPattern` | `path_mode`, `search_prefix`, `search_count`, `path_variable`, `where_predicate` | Path with mode/search: `SHORTEST 3 TRAIL PATHS` |
| `ASTGraphPattern` | (children) | Container for path patterns |
| `ASTGraphReturnItem` | `alias` | Single return item with optional alias |
| `ASTGraphReturnClause` | `distinct`, `group_by`, `order_by`, `limit`, `offset` | Full RETURN clause |
| `ASTGraphQuery` | `graph_name`, `is_optional_match`, `match_pattern`, `where_condition`, `return_clause` | Top-level query |

### Enums

| Enum | Values |
|------|--------|
| `GraphEdgeDirection` | `RIGHT`, `LEFT`, `UNDIRECTED`, `LEFT_OR_UNDIRECTED`, `UNDIRECTED_OR_RIGHT`, `LEFT_OR_RIGHT`, `ANY` |
| `GraphPathMode` | `DEFAULT`, `WALK`, `TRAIL`, `SIMPLE`, `ACYCLIC` |
| `GraphPathSearch` | `NONE`, `ALL`, `ANY`, `SHORTEST`, `ALL_SHORTEST`, `ANY_SHORTEST`, `COUNTED_SHORTEST`, `COUNTED_SHORTEST_GROUP` |
| `GraphLabelOp` | `NAME`, `CONJUNCTION`, `DISJUNCTION`, `NEGATION`, `WILDCARD` |

### Visitor Implementation (`GQLASTBuilder`)

Extends `GQLBaseVisitor` with the following overrides:

**Match & Pattern (8 methods):**
`visitSimpleMatchStatement`, `visitGraphPattern`, `visitPathPattern`, `visitPpePathTerm`, `visitPathTerm`, `visitElementPattern`, `visitNodePattern`, `visitEdgePattern`

**Path Quantifiers (3 methods):**
`visitPfPathPrimary`, `visitPfQuantifiedPathPrimary`, `visitPfQuestionedPathPrimary`

**Parenthesized Paths (2 methods):**
`visitPpParenthesizedPathPatternExpression`, `visitParenthesizedPathPatternExpression`

**Label Expressions (6 methods):**
`visitLabelExpressionName`, `visitLabelExpressionWildcard`, `visitLabelExpressionNegation`, `visitLabelExpressionConjunction`, `visitLabelExpressionDisjunction`, `visitLabelExpressionParenthesized`

**Value Expressions (12 methods):**
`visitComparisonExprAlt`, `visitConjunctiveExprAlt`, `visitDisjunctiveExprAlt`, `visitNotExprAlt`, `visitAddSubtractExprAlt`, `visitMultDivExprAlt`, `visitConcatenationExprAlt`, `visitSignedExprAlt`, `visitIsNotExprAlt`, `visitPrimaryExprAlt`, `visitValueExpressionPrimary`, `visitParenthesizedValueExpression`

**Literals & Variables (6 methods):**
`visitBindingVariableReference`, `visitUnsignedValueSpecification`, `visitNonNegativeIntegerSpecification`, `visitUnsignedLiteral`, `visitUnsignedNumericLiteral`, `visitGeneralLiteral`

**RETURN (4 methods):**
`visitPrimitiveResultStatement`, `visitReturnStatement`, `visitAggregatingValueExpression`, `visitOrderByAndPageStatement`

**WHERE (2 methods):**
`visitSearchCondition`, `visitBooleanValueExpression`

### Expression Mapping Strategy

GQL expressions are mapped to ClickHouse AST:

| GQL Expression | ClickHouse AST |
|---------------|----------------|
| `a = b` | `ASTFunction("equals", [a, b])` |
| `a <> b` | `ASTFunction("notEquals", [a, b])` |
| `a AND b` | `ASTFunction("and", [a, b])` |
| `a OR b` | `ASTFunction("or", [a, b])` |
| `NOT a` | `ASTFunction("not", [a])` |
| `a + b` | `ASTFunction("plus", [a, b])` |
| `a.prop` | `ASTFunction("tupleElement", [a, "prop"])` |
| Variable `x` | `ASTIdentifier("x")` |
| Numeric `42` | `ASTLiteral(42)` |
| String `'hello'` | `ASTLiteral("hello")` |
| `NULL` | `ASTLiteral(Null)` |

## Supported GQL Subset

```gql
-- Node scan with label
MATCH (a:Person)

-- Edge traversal with all 7 direction types
MATCH (a:Person)-[e:KNOWS]->(b:Person)   -- RIGHT
MATCH (a)<-[e:FOLLOWS]-(b)               -- LEFT
MATCH (a)~[e:SIMILAR]~(b)                -- UNDIRECTED
MATCH (a)-[e]->(b)                        -- ANY

-- Label expressions with boolean operators
MATCH (n:Person&Employee)                 -- Conjunction
MATCH (n:Person|Company)                  -- Disjunction
MATCH (n:!Person)                         -- Negation
MATCH (n:%)                               -- Wildcard

-- Variable-length paths with quantifiers
MATCH (a)-[e:KNOWS*]->(b)                -- * (0..inf)
MATCH (a)-[e:KNOWS+]->(b)                -- + (1..inf)
MATCH (a)-[e:KNOWS?]->(b)                -- ? (0..1)
MATCH (a)-[e:KNOWS{3}]->(b)              -- {n} (exactly n)
MATCH (a)-[e:KNOWS{2,5}]->(b)            -- {n,m} (range)

-- Path modes and search prefixes
MATCH TRAIL (a)-[]->(b)                   -- TRAIL mode
MATCH SHORTEST 3 (a)-[]->(b)             -- SHORTEST n search
MATCH ALL SHORTEST (a)-[]->(b)           -- ALL SHORTEST

-- WHERE clause with expressions
MATCH (a:Person) WHERE a.age > 30
MATCH (a)-[e]->(b) WHERE a.name = 'Alice' AND b.city <> 'NYC'

-- RETURN with aliases, DISTINCT, ORDER BY, LIMIT, OFFSET
MATCH (a:Person) RETURN a.name AS name, a.age
MATCH (a:Person) RETURN DISTINCT a.city ORDER BY a.name LIMIT 10 OFFSET 5
```

## Not Yet Implemented (Parser-Level)

- CREATE PROPERTY GRAPH DDL (grammar parsed, no AST builder)
- INSERT / SET / DELETE DML
- OPTIONAL MATCH (AST field exists, visitor not connected)
- UNION / EXCEPT / INTERSECT
- Subqueries and EXISTS predicate
- LET / FOR clauses
- CALL procedures
- SELECT statement (alternative to RETURN)
- CASE expressions
- Aggregate functions (COUNT, SUM, etc.)

## Build and Test Results

### Compilation
- Full `clickhouse` binary builds successfully.
- Generated ANTLR4 code in `src/Parsers/Graph/generated/` with `-w` flag.
- `GQLParsingUtil.cpp` uses `#pragma clang diagnostic` for ANTLR headers.

### Runtime Tests
```
$ clickhouse local --query "MATCH (n:Person)"
Code: 78. UNKNOWN_TYPE_OF_QUERY  (expected - no InterpreterGraphQuery yet)

$ clickhouse local --query "MATCH (a:Person)-[e:KNOWS]->(b:Person)"
Code: 78. UNKNOWN_TYPE_OF_QUERY  (expected)

$ clickhouse local --query "SELECT 42 AS answer"
42  (standard SQL unaffected)
```

## Next Steps

1. Implement `InterpreterGraphQuery` (translate Graph AST to ClickHouse query plan)
2. Implement Graph Catalog (`CREATE PROPERTY GRAPH` mapping)
3. Implement expand-based graph operators (`GraphScanStep`, `GraphExpandStep`)
4. Add unit tests for the parser
5. Connect DDL/DML visitors
