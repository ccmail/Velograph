---
description: 'Phased implementation roadmap for Graph-on-ClickHouse'
sidebar_label: 'Roadmap'
sidebar_position: 85
slug: /development/graph/roadmap
title: 'Implementation Roadmap'
doc_type: 'reference'
---

# Implementation Roadmap

This document outlines the phased implementation plan for Graph-on-ClickHouse, organized by deliverable milestones.

## Phase Overview

```
P0: ANTLR4 GQL Parser ──> P1: Graph Catalog ──> P2: Single-hop Queries
     (parse GQL)            (DDL + metadata)      (point + 1-hop)
         |                       |                      |
         2-3 weeks               2-3 weeks              3-4 weeks
                                                        |
P3: Multi-hop Queries ──> P4: Optimization ──> P5: Advanced Features
     (BFS expand *N)       (predicate pushdown)  (algorithms, distributed)
         |                       |                      |
         2-3 weeks               2 weeks                ongoing
```

## P0: ANTLR4 GQL Parser Integration

**Goal**: Parse basic GQL syntax and produce a Graph AST within ClickHouse.

### Tasks

| # | Task | Details |
|---|------|---------|
| 0.1 | Import GQL grammar | Copy `GQL.g4` from opengql/grammar to `contrib/gql-grammar/` |
| 0.2 | CMake setup | Create `contrib/gql-grammar-cmake/CMakeLists.txt` to generate C++ Lexer/Parser |
| 0.3 | Grammar adaptation | Trim or adapt `GQL.g4` for the supported subset (remove unsupported rules, resolve ambiguities) |
| 0.4 | AST node classes | Implement `ASTGraphQuery`, `ASTGraphPattern`, `ASTNodePattern`, `ASTEdgePattern`, etc. |
| 0.5 | ANTLR visitor | Implement `GQLParsingUtil` to convert ANTLR parse tree to Graph AST |
| 0.6 | Parser entry point | Create `ParserGraphQuery` and register in `ParserQuery::parseImpl` |
| 0.7 | Unit tests | Test parsing of: node pattern, edge pattern, simple MATCH, MATCH with WHERE |

### Acceptance Criteria

- `MATCH (a:Person) RETURN a.name` parses successfully and produces `ASTGraphQuery`.
- `MATCH (a:Person)-[:FOLLOWS]->(b:Person) WHERE a.name = 'Alice' RETURN b.name` parses correctly.
- Parse errors produce clear ClickHouse exceptions with line/position info.
- `EXPLAIN AST MATCH (a:Person) RETURN a` shows the Graph AST structure.

### Files to Create

```
contrib/gql-grammar/GQL.g4
contrib/gql-grammar-cmake/CMakeLists.txt
src/Parsers/Graph/ParserGraphQuery.h
src/Parsers/Graph/ParserGraphQuery.cpp
src/Parsers/Graph/GQLParsingUtil.h
src/Parsers/Graph/GQLParsingUtil.cpp
src/Parsers/Graph/ASTGraphQuery.h
src/Parsers/Graph/ASTGraphPattern.h
src/Parsers/Graph/ASTNodePattern.h
src/Parsers/Graph/ASTEdgePattern.h
src/Parsers/Graph/ASTPathPattern.h
src/Parsers/Graph/ASTReturnClause.h
src/Parsers/Graph/ASTGraphWhereClause.h
src/Parsers/Graph/ASTLabelExpression.h
src/Parsers/Graph/ASTCreateGraphStatement.h
src/Parsers/Graph/CMakeLists.txt
src/Parsers/Graph/tests/gtest_gql_parser.cpp
```

### Files to Modify

```
contrib/CMakeLists.txt              -- add_contrib(gql-grammar-cmake gql-grammar)
src/Parsers/CMakeLists.txt          -- add_subdirectory(Graph)
src/Parsers/ParserQuery.cpp         -- Register ParserGraphQuery
```

---

## P1: Graph Catalog and DDL

**Goal**: Create and manage property graph definitions that map to existing ClickHouse tables.

### Tasks

| # | Task | Details |
|---|------|---------|
| 1.1 | Data structures | Implement `GraphDefinition`, `VertexTableMapping`, `EdgeTableMapping` |
| 1.2 | GraphCatalog | Implement singleton catalog with CRUD operations |
| 1.3 | Persistence | Save/load graph definitions to `metadata/graphs/*.graph.sql` |
| 1.4 | CREATE GRAPH parser | Parse `CREATE PROPERTY GRAPH` via ANTLR4 or hand-written parser |
| 1.5 | DROP GRAPH parser | Parse `DROP PROPERTY GRAPH` |
| 1.6 | InterpreterCreateGraph | Validate table/column references, create graph definition |
| 1.7 | InterpreterDropGraph | Remove graph definition |
| 1.8 | System tables | Implement `system.graphs`, `system.graph_vertices`, `system.graph_edges` |
| 1.9 | InterpreterFactory | Register graph interpreters |
| 1.10 | Tests | DDL tests: create, drop, system table queries |

### Acceptance Criteria

- `CREATE PROPERTY GRAPH g VERTEX TABLES (...) EDGE TABLES (...)` succeeds.
- `DROP PROPERTY GRAPH g` removes the definition.
- `SELECT * FROM system.graphs` shows registered graphs.
- `SELECT * FROM system.graph_vertices WHERE graph = 'g'` shows vertex mappings.
- Graph definition survives server restart (metadata persistence).
- Referencing non-existent tables or columns produces clear errors.

### Files to Create

```
src/Graph/GraphCatalog.h
src/Graph/GraphCatalog.cpp
src/Graph/GraphDefinition.h
src/Graph/VertexTableMapping.h
src/Graph/EdgeTableMapping.h
src/Graph/GraphCatalogStorage.h
src/Graph/GraphCatalogStorage.cpp
src/Graph/CMakeLists.txt
src/Interpreters/InterpreterCreateGraph.h
src/Interpreters/InterpreterCreateGraph.cpp
src/Interpreters/InterpreterDropGraph.h
src/Interpreters/InterpreterDropGraph.cpp
src/Storages/System/StorageSystemGraphs.h
src/Storages/System/StorageSystemGraphs.cpp
src/Storages/System/StorageSystemGraphVertices.h
src/Storages/System/StorageSystemGraphVertices.cpp
src/Storages/System/StorageSystemGraphEdges.h
src/Storages/System/StorageSystemGraphEdges.cpp
```

### Files to Modify

```
src/CMakeLists.txt                         -- add_subdirectory(Graph)
src/Interpreters/InterpreterFactory.cpp    -- Register graph interpreters
src/Storages/System/attachSystemTables.cpp -- Register system tables
```

---

## P2: Single-Hop Graph Queries

**Goal**: Execute basic graph pattern matching (vertex scan, single-hop expand).

### Tasks

| # | Task | Details |
|---|------|---------|
| 2.1 | GraphScanStep | Implement vertex/edge table scan wrapping `ReadFromMergeTree` |
| 2.2 | GraphExpandStep | Implement single-hop expand with IN-list probe |
| 2.3 | GraphExpandTransform | Core expand processor |
| 2.4 | GraphVertexLookupTransform | Vertex property lookup after expand |
| 2.5 | InterpreterGraphQuery | Pattern analysis, plan construction, execution |
| 2.6 | Graph variable resolution | Map `a.name` to `users.name` based on graph catalog |
| 2.7 | WHERE clause integration | Filter pushdown to scan and expand steps |
| 2.8 | RETURN clause handling | Projection with alias support |
| 2.9 | End-to-end tests | Vertex scan, 1-hop outgoing, 1-hop incoming, 2-hop chain |

### Acceptance Criteria

- `MATCH (a:Person) WHERE a.age > 25 RETURN a.name` returns correct results.
- `MATCH (a:Person)-[:FOLLOWS]->(b:Person) WHERE a.name = 'Alice' RETURN b.name` works.
- `MATCH (a:Person)-[:FOLLOWS]->(b:Person)-[:WORKS_AT]->(c:Company) RETURN c.name` works.
- `MATCH (a:Person)<-[:FOLLOWS]-(b:Person) RETURN b.name` (incoming edge) works.
- WHERE conditions are pushed down to table scans (verify via EXPLAIN).

### Files to Create

```
src/Processors/QueryPlan/GraphScanStep.h
src/Processors/QueryPlan/GraphScanStep.cpp
src/Processors/QueryPlan/GraphExpandStep.h
src/Processors/QueryPlan/GraphExpandStep.cpp
src/Processors/Transforms/GraphExpandTransform.h
src/Processors/Transforms/GraphExpandTransform.cpp
src/Processors/Transforms/GraphVertexLookupTransform.h
src/Processors/Transforms/GraphVertexLookupTransform.cpp
src/Interpreters/InterpreterGraphQuery.h
src/Interpreters/InterpreterGraphQuery.cpp
```

---

## P3: Multi-Hop Graph Queries

**Goal**: Support variable-length path patterns with iterative BFS.

### Tasks

| # | Task | Details |
|---|------|---------|
| 3.1 | GraphMultiHopExpandStep | Iterative BFS with frontier tracking |
| 3.2 | Visited-set tracking | Prevent cycles, configurable via settings |
| 3.3 | Path recording | Optionally record vertex path for RETURN |
| 3.4 | Depth limits | `max_graph_expansion_depth` setting |
| 3.5 | ASTPathQuantifier | Parse `*`, `+`, `*1..N`, `*..N`, `*N..` |
| 3.6 | Tests | Multi-hop queries at various depths, cycle handling |

### Acceptance Criteria

- `MATCH (a)-[:FOLLOWS*1..3]->(b) WHERE a.name = 'Alice' RETURN b.name` returns correct results.
- `MATCH (a)-[:FOLLOWS*]->(b) WHERE a.name = 'Alice' RETURN b.name` traverses up to `max_graph_expansion_depth`.
- Cycles are correctly handled (no infinite loops).
- `MATCH (a)-[:FOLLOWS*2]->(b)` returns only 2-hop results (exact depth).
- Large graph traversals respect memory limits.

---

## P4: Optimization Integration

**Goal**: Leverage ClickHouse optimizer for graph query performance.

### Tasks

| # | Task | Details |
|---|------|---------|
| 4.1 | Predicate pushdown | Push WHERE conditions to `GraphScanStep` filters |
| 4.2 | Column pruning | Only read required columns from underlying tables |
| 4.3 | Selectivity-based expand order | Start expand from the most selective vertex |
| 4.4 | Frontier batching | Batch IN-list queries for expand efficiency |
| 4.5 | EXPLAIN GRAPH | Show graph-specific query plan |
| 4.6 | Performance benchmarks | Compare with SQL equivalent queries |

### Acceptance Criteria

- `EXPLAIN` shows pushed-down predicates in `GraphScanStep`.
- Query performance is competitive with hand-written SQL equivalents.
- `EXPLAIN GRAPH MATCH ...` shows the expand plan with estimated sizes.

---

## P5: Advanced Features (Future)

| Feature | Description | Dependency |
|---------|-------------|------------|
| Shortest path | `ANY SHORTEST` path between vertices | P3 |
| OPTIONAL MATCH | Left-outer-join semantics for optional patterns | P2 |
| Graph algorithms | PageRank, connected components, community detection | P3 |
| INSERT/DELETE | Graph DML via GQL syntax | P1 |
| Multi-graph | `USE graphExpression` to switch graphs | P1 |
| Distributed | Cross-shard graph expansion | P3 |
| Aggregations | `COUNT`, `SUM`, `AVG` in graph context | P2 |
| Path modes | `WALK`, `TRAIL`, `SIMPLE`, `ACYCLIC` semantics | P3 |

## Testing Strategy

### Test Levels

| Level | Location | Description |
|-------|----------|-------------|
| Unit | `src/*/tests/gtest_*.cpp` | Individual component tests |
| Stateless SQL | `tests/queries/0_stateless/` | End-to-end SQL tests |
| Integration | `tests/integration/test_graph/` | Multi-node graph queries |

### Test Data

Standard test datasets for graph testing:

```sql
-- Small social network (for unit/stateless tests)
CREATE TABLE test_users (id UInt64, name String, age UInt32)
    ENGINE = MergeTree ORDER BY id;
CREATE TABLE test_follows (id UInt64, src UInt64, dst UInt64, ts DateTime)
    ENGINE = MergeTree ORDER BY (src, dst);

INSERT INTO test_users VALUES (1,'Alice',30),(2,'Bob',25),(3,'Carol',28);
INSERT INTO test_follows VALUES (1,1,2,now()),(2,1,3,now()),(3,2,3,now());

CREATE PROPERTY GRAPH test_graph
  VERTEX TABLES (test_users KEY(id) LABEL Person PROPERTIES(id,name,age))
  EDGE TABLES (test_follows KEY(id)
    SOURCE KEY(src) REFERENCES test_users(id)
    DESTINATION KEY(dst) REFERENCES test_users(id)
    LABEL FOLLOWS PROPERTIES(ts));
```

### Benchmark Queries

| Query | Type | Expected Behavior |
|-------|------|-------------------|
| `MATCH (a:Person) RETURN count(*)` | Vertex scan | Full table scan, should match `SELECT count(*) FROM users` |
| `MATCH (a)-[:FOLLOWS]->(b) WHERE a.name='Alice' RETURN b` | 1-hop | Selective start + expand |
| `MATCH (a)-[:FOLLOWS]->(b)-[:FOLLOWS]->(c) WHERE a.name='Alice' RETURN c` | 2-hop | Two expand steps |
| `MATCH (a)-[:FOLLOWS*1..10]->(b) WHERE a.name='Alice' RETURN count(*)` | Multi-hop | Iterative BFS up to 10 hops |
