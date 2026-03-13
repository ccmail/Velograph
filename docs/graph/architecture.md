---
description: 'Detailed architecture of the Graph-on-ClickHouse engine'
sidebar_label: 'Architecture'
sidebar_position: 81
slug: /development/graph/architecture
title: 'Graph Engine Architecture'
doc_type: 'reference'
---

# Graph Engine Architecture

This document describes the detailed architecture of Graph-on-ClickHouse, including module interactions, data flow, and integration points with the ClickHouse core.

## System Layers

```
+================================================================+
|                       GQL Query Text                            |
+================================================================+
                              |
+-----------------------------v----------------------------------+
| Layer 1: Parsing                                                |
|                                                                 |
|  ParserGraphQuery                                               |
|    -> ANTLR4 GQL Lexer/Parser (generated from GQL.g4)          |
|    -> GQLParsingUtil: ANTLR ParseTree -> Graph AST              |
|                                                                 |
|  Output: ASTGraphQuery (or ASTCreateGraphStatement, etc.)       |
+---------------------------------+------------------------------+
                                  |
+---------------------------------v------------------------------+
| Layer 2: Interpretation                                         |
|                                                                 |
|  InterpreterFactory                                             |
|    -> ASTGraphQuery       => InterpreterGraphQuery              |
|    -> ASTCreateGraphStmt  => InterpreterCreateGraph             |
|    -> ASTDropGraphStmt    => InterpreterDropGraph               |
|                                                                 |
|  InterpreterGraphQuery:                                         |
|    1. Resolve graph from GraphCatalog                           |
|    2. Extract MATCH patterns from AST                           |
|    3. Build expand-based QueryPlan                              |
|    4. Apply filters from WHERE clause                           |
|    5. Build RETURN projection                                   |
|                                                                 |
|  Output: QueryPlan (tree of IQueryPlanStep)                     |
+---------------------------------+------------------------------+
                                  |
+---------------------------------v------------------------------+
| Layer 3: Graph Query Plan                                       |
|                                                                 |
|  Graph-specific steps:                                          |
|    GraphScanStep        - Scan vertex/edge table                |
|    GraphExpandStep      - Single-hop BFS expand                 |
|    GraphMultiHopStep    - Multi-hop iterative BFS               |
|                                                                 |
|  Reused ClickHouse steps:                                       |
|    FilterStep           - WHERE conditions                      |
|    ExpressionStep       - Property access, computations         |
|    SortingStep          - ORDER BY                              |
|    LimitStep            - LIMIT                                 |
|    DistinctStep         - DISTINCT                              |
|    AggregatingStep      - Aggregations in RETURN                |
|                                                                 |
|  Output: QueryPipeline (DAG of IProcessor)                      |
+---------------------------------+------------------------------+
                                  |
+---------------------------------v------------------------------+
| Layer 4: Pipeline Execution (reused from ClickHouse)            |
|                                                                 |
|  Graph-specific processors:                                     |
|    GraphExpandTransform       - Core expand logic               |
|    GraphVertexLookupTransform - Vertex property retrieval       |
|    GraphFrontierTracker       - Visited-set management          |
|                                                                 |
|  Reused ClickHouse processors:                                  |
|    ReadFromMergeTree          - Table reads                     |
|    FilterTransform            - Row filtering                   |
|    ExpressionTransform        - Expression evaluation           |
|                                                                 |
|  PipelineExecutor drives execution                              |
+---------------------------------+------------------------------+
                                  |
+---------------------------------v------------------------------+
| Layer 5: Storage (ClickHouse native)                            |
|                                                                 |
|  Vertex tables: standard MergeTree / ReplacingMergeTree         |
|  Edge tables:   standard MergeTree                              |
|  Graph catalog: metadata persisted to disk                      |
+----------------------------------------------------------------+
```

## Module Interactions

### Parsing -> Interpretation

The `ParserGraphQuery` parser is registered in `ParserQuery::parseImpl` alongside existing SQL parsers. When a GQL keyword (e.g., `MATCH`, `CREATE PROPERTY GRAPH`) is detected, the parser delegates to the ANTLR4 GQL parser and converts the result to a Graph AST node.

The `InterpreterFactory` maps Graph AST types to their interpreters:

| AST Type | Interpreter |
|----------|------------|
| `ASTGraphQuery` | `InterpreterGraphQuery` |
| `ASTCreateGraphStatement` | `InterpreterCreateGraph` |
| `ASTDropGraphStatement` | `InterpreterDropGraph` |

### Interpretation -> Query Plan

`InterpreterGraphQuery` is the central coordinator. Given a `MATCH` pattern, it:

1. Looks up the property graph definition in `GraphCatalog`.
2. Resolves each node/edge pattern to its underlying ClickHouse table via `VertexTableMapping` / `EdgeTableMapping`.
3. Determines the expand order (which vertex to start from, based on selectivity hints).
4. Generates a chain of `GraphScanStep` -> `GraphExpandStep` -> ... steps.
5. Attaches `FilterStep` for WHERE conditions, pushing predicates as close to scans as possible.
6. Adds projection for the RETURN clause.

### Query Plan -> Pipeline

Each `IQueryPlanStep::updatePipeline` method converts the logical step into physical processors:

- `GraphScanStep::updatePipeline` creates a `ReadFromMergeTree` source with appropriate filters.
- `GraphExpandStep::updatePipeline` creates a `GraphExpandTransform` that takes frontier IDs as input and produces expanded results.
- `GraphMultiHopStep::updatePipeline` creates an iterative loop (similar to `RecursiveCTESource`) that repeatedly applies single-hop expansion.

## Source Directory Layout

```
src/
  Graph/                              -- Graph catalog and definitions
    GraphCatalog.h/cpp
    GraphDefinition.h
    VertexTableMapping.h
    EdgeTableMapping.h
    GraphCatalogStorage.h/cpp

  Parsers/
    Graph/                            -- GQL parsing
      ParserGraphQuery.h/cpp
      GQLParsingUtil.h/cpp
      ASTGraphQuery.h
      ASTGraphPattern.h
      ASTNodePattern.h
      ASTEdgePattern.h
      ASTPathPattern.h
      ASTReturnClause.h
      ASTCreateGraphStatement.h

  Interpreters/
    InterpreterGraphQuery.h/cpp       -- MATCH query execution
    InterpreterCreateGraph.h/cpp      -- CREATE PROPERTY GRAPH
    InterpreterDropGraph.h/cpp        -- DROP PROPERTY GRAPH

  Processors/
    QueryPlan/
      GraphScanStep.h/cpp             -- Vertex/edge table scan
      GraphExpandStep.h/cpp           -- Single-hop expand
      GraphMultiHopExpandStep.h/cpp   -- Multi-hop iterative expand
    Transforms/
      GraphExpandTransform.h/cpp      -- Core expand processor
      GraphVertexLookupTransform.h/cpp -- Vertex property lookup

  Storages/
    System/
      StorageSystemGraphs.h/cpp       -- system.graphs
      StorageSystemGraphVertices.h/cpp -- system.graph_vertices
      StorageSystemGraphEdges.h/cpp   -- system.graph_edges

contrib/
  gql-grammar/GQL.g4                 -- GQL ANTLR4 grammar
  gql-grammar-cmake/CMakeLists.txt   -- Grammar build configuration
```

## Integration with ClickHouse Core

### Minimal Invasive Changes

The graph engine integrates with ClickHouse through well-defined extension points:

1. **Parser registration**: One `if` branch added to `ParserQuery::parseImpl`.
2. **Interpreter registration**: Three entries added to `InterpreterFactory`.
3. **System table registration**: Three entries added to `attachSystemTablesServer`.
4. **Build system**: Two `add_contrib` entries in `contrib/CMakeLists.txt`.

All graph-specific logic lives in dedicated directories (`src/Graph/`, `src/Parsers/Graph/`, `src/Processors/QueryPlan/Graph*`, `src/Processors/Transforms/Graph*`), minimizing impact on existing code.

### Reused ClickHouse Components

| Component | How it is reused |
|-----------|-----------------|
| `ReadFromMergeTree` | Scan vertex/edge tables |
| `FilterTransform` | Apply WHERE conditions |
| `ExpressionTransform` | Evaluate property expressions |
| `SortingStep` / `LimitStep` | ORDER BY / LIMIT in RETURN |
| `AggregatingStep` | Aggregations in RETURN |
| `DatabaseCatalog` | Resolve underlying table references |
| `StorageFactory` | Create tables for graph DDL |
| `Context` | Settings, authentication, quotas |
| ANTLR4 C++ Runtime | Parse GQL (already in contrib for PromQL) |

### Threading and Resource Management

Graph query execution reuses ClickHouse's thread pool and memory tracking:

- `GraphExpandTransform` executes within the standard `PipelineExecutor` thread pool.
- Memory is tracked through ClickHouse's `MemoryTracker` (per-query limits apply).
- Multi-hop iterations are bounded by a configurable `max_graph_expansion_depth` setting.
