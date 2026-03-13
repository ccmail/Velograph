---
description: 'Graph-on-ClickHouse: an analytical graph query engine built on top of ClickHouse'
sidebar_label: 'Graph Engine Overview'
sidebar_position: 80
slug: /development/graph
title: 'Graph-on-ClickHouse'
doc_type: 'reference'
---

# Graph-on-ClickHouse

Graph-on-ClickHouse is an analytical graph query engine built on top of ClickHouse. It provides graph query and graph analysis capabilities for existing ClickHouse data while preserving ClickHouse's native storage and execution power.

## Goals

- **Not a standalone graph store.** Leverage ClickHouse's existing `MergeTree` storage, vectorized execution, and distributed capabilities.
- **Table mapping.** Map existing ClickHouse tables as vertex tables and edge tables, or create vertex/edge tables via graph DDL (still backed by native ClickHouse tables).
- **Standard query language.** Support GQL (ISO/IEC 39075), the standardized property graph query language, via an ANTLR4-based parser.
- **OLAP-oriented.** Designed for analytical workloads (batch graph queries, multi-hop aggregations, graph pattern matching), not for transactional graph operations.

## Non-Goals

- Replace dedicated graph databases for OLTP graph workloads.
- Implement a new storage engine from scratch.
- Support real-time graph mutations at high throughput (single-row inserts/deletes).

## Architecture Overview

The engine is organized into three core modules:

| Module | Responsibility |
|--------|---------------|
| **GQL Parser** | ANTLR4-based GQL parsing, producing a Graph AST that integrates with ClickHouse's query processing pipeline. |
| **Graph Operators** | Expand-based (BFS) execution model with graph-specific `QueryPlanStep` and `IProcessor` implementations. |
| **Graph Catalog** | Metadata layer that manages property graph definitions and maps them to underlying ClickHouse tables. |

```
                         GQL Query Text
                              |
                    +---------v----------+
                    |   GQL Parser       |   ANTLR4 Lexer/Parser
                    |   -> Graph AST     |   GQL.g4 -> C++ code
                    +---------+----------+
                              |
                    +---------v----------+
                    |  InterpreterGraph  |   Graph query interpretation
                    |  Query             |   Pattern -> Expand plan
                    +---------+----------+
                              |
                    +---------v----------+
                    |  Graph QueryPlan   |   GraphScanStep
                    |  Steps             |   GraphExpandStep
                    |                    |   GraphMultiHopStep
                    +---------+----------+
                              |
                    +---------v----------+
                    |  ClickHouse        |   ReadFromMergeTree
                    |  Pipeline          |   FilterTransform
                    |  (reused)          |   ExpressionTransform
                    +---------+----------+
                              |
                    +---------v----------+
                    |  ClickHouse        |   MergeTree tables
                    |  Storage           |   (vertex & edge data)
                    +--------------------+
```

## Key Design Decisions

### Expand-Based Execution (not Join-Based)

Graph pattern matching uses a BFS-style expand model rather than translating patterns directly to SQL joins. This approach:

- Naturally maps to graph traversal semantics (frontier expansion).
- Handles variable-length paths (`*1..N`) via iterative BFS with visited-set tracking.
- Allows early termination and path pruning.
- Aligns with how graph databases execute queries internally.

Each expand step takes a set of frontier vertex IDs, probes the edge table to find neighbors, and produces the next frontier.

### ANTLR4 Parser (not Hand-Written)

ClickHouse already integrates the ANTLR4 C++ runtime for PromQL parsing. We reuse this infrastructure for GQL, which allows:

- Strict compliance with the ISO GQL standard grammar.
- Easier maintenance as the GQL standard evolves.
- A proven integration pattern within the ClickHouse codebase.

### Reuse ClickHouse Optimizer

Graph query optimization (predicate pushdown, scan filtering) reuses ClickHouse's existing optimizer infrastructure rather than building a separate graph optimizer. Graph-specific optimizations are limited to:

- Expand order selection (which vertex/edge to expand first).
- Frontier size estimation for adaptive execution.
- Visited-set pruning strategies for multi-hop queries.

## Documentation Index

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | Detailed system architecture and module interactions |
| [GQL Parser](parser.md) | ANTLR4 integration, AST design, and parser pipeline |
| [Graph Operators](operators.md) | Expand execution model, `QueryPlanStep` and `IProcessor` designs |
| [Graph Catalog](catalog.md) | Property graph definitions, table mapping, and metadata storage |
| [Roadmap](roadmap.md) | Phased implementation plan with milestones |

## Example

```sql
-- Create a property graph over existing ClickHouse tables
CREATE PROPERTY GRAPH social_network
  VERTEX TABLES (
    users KEY (user_id) LABEL Person PROPERTIES (user_id AS id, name, age)
  )
  EDGE TABLES (
    follows KEY (follow_id)
      SOURCE KEY (follower_id) REFERENCES users (user_id)
      DESTINATION KEY (followee_id) REFERENCES users (user_id)
      LABEL FOLLOWS PROPERTIES (created_at)
  );

-- Query: find friends-of-friends for Alice
MATCH (a:Person)-[:FOLLOWS]->(b:Person)-[:FOLLOWS]->(c:Person)
WHERE a.name = 'Alice'
RETURN DISTINCT c.name, c.age;

-- Variable-length path: find all reachable persons within 5 hops
MATCH (a:Person)-[:FOLLOWS*1..5]->(b:Person)
WHERE a.name = 'Alice'
RETURN b.name, b.age;
```
