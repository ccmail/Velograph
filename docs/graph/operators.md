---
description: 'Graph operator design: expand-based execution model'
sidebar_label: 'Graph Operators'
sidebar_position: 83
slug: /development/graph/operators
title: 'Graph Operators - Expand Execution Model'
doc_type: 'reference'
---

# Graph Operators - Expand Execution Model

This document describes the expand-based (BFS) execution model for graph queries, including the design of graph-specific `QueryPlanStep` and `IProcessor` implementations.

## Why Expand, Not Join

Traditional relational approaches translate graph patterns to SQL joins:

```sql
-- MATCH (a:Person)-[:FOLLOWS]->(b:Person)
SELECT a.*, b.*
FROM users a
JOIN follows f ON a.user_id = f.follower_id
JOIN users b ON f.followee_id = b.user_id
```

The expand model instead works like a graph traversal:

```
1. Start with frontier = scan(users, filter=...)
2. Expand frontier via edge table follows
3. Lookup destination vertices from users
```

### Advantages of Expand

| Aspect | Join Model | Expand Model |
|--------|-----------|--------------|
| **Variable-length paths** | Requires recursive CTE or self-join unrolling | Natural iterative BFS loop |
| **Early termination** | Difficult (full join first, then filter) | Stop when frontier is empty or depth limit reached |
| **Visited tracking** | Requires extra logic per join level | Built into the expand loop |
| **Selectivity** | Always processes full tables | Starts from selective frontier, prunes early |
| **Conceptual clarity** | Graph semantics hidden in joins | Direct graph traversal semantics |

### When Join is Still Used

For simple 1-hop patterns without variable-length paths, the expand model effectively reduces to a hash-join. The implementation uses ClickHouse's native join capabilities internally when beneficial.

## Core Operators

### GraphScanStep

Scans a vertex table or edge table, applying filters. Wraps `ReadFromMergeTree` with graph-aware column mapping.

```
Input:  None (source step)
Output: Block with vertex/edge columns mapped to graph properties

Parameters:
  - table_mapping: VertexTableMapping or EdgeTableMapping
  - filter: optional predicate (pushed down from WHERE)
  - required_columns: columns needed by downstream operators
```

Implementation outline:

```cpp
class GraphScanStep : public IQueryPlanStep
{
public:
    GraphScanStep(
        const VertexTableMapping & mapping,
        const ActionsDAG * filter,
        const Names & required_columns,
        ContextPtr context);

    void updatePipeline(QueryPipelineBuilders pipelines,
                        const BuildQueryPipelineSettings & settings) override
    {
        // Translate graph property names to underlying table column names
        // Create ReadFromMergeTree with pushed-down filter
        // Rename output columns from table names to graph property names
    }

    String getName() const override { return "GraphScan"; }
};
```

### GraphExpandStep

The core expand operator. Takes a set of frontier vertex IDs and expands through an edge table to find neighbors.

```
Input:  Block containing frontier vertex IDs
Output: Block containing (source_id, edge_properties..., dest_id)

Parameters:
  - edge_mapping: EdgeTableMapping (which table, src/dst columns)
  - direction: OUTGOING | INCOMING | BOTH
  - edge_filter: optional predicate on edge properties
```

#### Execution Flow

```
                     Input Block
                  (frontier IDs: [1, 5, 8])
                         |
            +------------v-----------+
            | GraphExpandTransform   |
            |                        |
            | 1. Extract ID column   |
            | 2. Build IN-list       |
            | 3. Query edge table:   |
            |    SELECT src, dst,    |
            |           props...     |
            |    FROM edges          |
            |    WHERE src IN (...)  |
            | 4. Output results      |
            +------------+-----------+
                         |
                    Output Block
              (src: [1,1,5], dst: [2,3,7],
               edge_props: [...])
```

#### Implementation Approach

```cpp
class GraphExpandTransform : public IProcessor
{
    /// Edge table information
    EdgeTableMapping edge_mapping;
    EdgeDirection direction;

    /// Underlying storage for edge table
    StoragePtr edge_storage;

    /// Context for creating internal queries
    ContextPtr context;

    Status prepare() override;
    void work() override
    {
        // 1. Read frontier IDs from input chunk
        auto frontier_ids = extractIDColumn(input_chunk);

        // 2. Build a filter: edge.source_column IN (frontier_ids)
        auto filter = buildINFilter(frontier_ids, edge_mapping.source_column);

        // 3. Read from edge table with filter
        //    Uses ReadFromMergeTree internally
        auto edge_data = readEdgeTable(edge_storage, filter, context);

        // 4. Output: (source_id, dest_id, edge_properties...)
        output_chunk = buildOutputChunk(edge_data);
    }
};
```

### GraphMultiHopExpandStep

Handles variable-length path patterns (`*min..max`). Implements iterative BFS with visited-set tracking.

```
Input:  Block containing start vertex IDs
Output: Block containing (start_id, path_length, end_id, [path])

Parameters:
  - edge_mapping: EdgeTableMapping
  - direction: OUTGOING | INCOMING | BOTH
  - min_hops: minimum path length
  - max_hops: maximum path length
  - edge_filter: optional predicate
  - track_path: whether to record full path (for RETURN path)
```

#### Iterative BFS Algorithm

```
function MultiHopExpand(start_vertices, edge_table, min_hops, max_hops):
    frontier = start_vertices
    visited = {}
    results = []
    depth = 0

    while depth < max_hops AND frontier is not empty:
        depth += 1

        // Single-hop expand
        expanded = Expand(frontier, edge_table)

        // Remove already visited vertices
        new_vertices = expanded.dest_ids - visited

        // Collect results if depth >= min_hops
        if depth >= min_hops:
            results += new_vertices (with path info)

        // Update state
        visited += new_vertices
        frontier = new_vertices

    return results
```

#### Similarity to RecursiveCTESource

This iterative pattern mirrors ClickHouse's `RecursiveCTESource` (see `src/Processors/Sources/RecursiveCTESource.cpp`):

| RecursiveCTE | MultiHopExpand |
|-------------|----------------|
| Working temporary table | Frontier vertex set |
| Intermediate temporary table | Expanded neighbor set |
| Swap tables per iteration | Swap frontier per iteration |
| Stop when no rows produced | Stop when frontier is empty or max_hops reached |
| `max_recursive_cte_evaluation_depth` | `max_hops` parameter |

The key difference is that `GraphMultiHopExpandStep` also maintains a visited set and collects intermediate results (for `min_hops > 0`).

### GraphVertexLookupTransform

After expanding edges, this transform retrieves vertex properties for the destination vertices.

```
Input:  Block containing vertex IDs (from expand output)
Output: Block with vertex IDs and their properties

Equivalent to:
  SELECT id, prop1, prop2, ...
  FROM vertex_table
  WHERE id IN (input_ids)
```

This is conceptually a semi-join lookup and uses `ReadFromMergeTree` with an IN-list filter.

## Query Plan Construction

### Single-Hop Example

```sql
MATCH (a:Person)-[:FOLLOWS]->(b:Person)
WHERE a.name = 'Alice'
RETURN b.name, b.age
```

Query Plan:

```
ProjectStep [b.name, b.age]
  |
  GraphVertexLookupStep [users -> b, by followee_id]
    |
    GraphExpandStep [follows, OUTGOING]
      |
      GraphScanStep [users -> a, filter: name = 'Alice', label: Person]
```

### Two-Hop Example

```sql
MATCH (a:Person)-[:FOLLOWS]->(b:Person)-[:WORKS_AT]->(c:Company)
WHERE a.name = 'Alice'
RETURN b.name, c.name
```

Query Plan:

```
ProjectStep [b.name, c.name]
  |
  GraphVertexLookupStep [companies -> c, by company_id]
    |
    GraphExpandStep [works_at, OUTGOING]
      |
      GraphVertexLookupStep [users -> b, by followee_id]
        |
        GraphExpandStep [follows, OUTGOING]
          |
          GraphScanStep [users -> a, filter: name = 'Alice']
```

### Multi-Hop Example

```sql
MATCH (a:Person)-[:FOLLOWS*1..5]->(b:Person)
WHERE a.name = 'Alice'
RETURN b.name
```

Query Plan:

```
ProjectStep [b.name]
  |
  GraphVertexLookupStep [users -> b]
    |
    GraphMultiHopExpandStep [follows, OUTGOING, hops: 1..5]
      |
      GraphScanStep [users -> a, filter: name = 'Alice']
```

## Predicate Pushdown

WHERE conditions are analyzed and pushed down to the earliest possible step:

| Predicate | Push Target |
|-----------|-------------|
| `a.name = 'Alice'` | `GraphScanStep` for vertex `a` |
| `e.weight > 0.5` | `GraphExpandStep` edge filter |
| `b.age > 25` | `GraphVertexLookupStep` for vertex `b` |
| `a.age + b.age > 50` | `FilterStep` after both vertices are available |

This reuses ClickHouse's existing predicate pushdown infrastructure. The graph interpreter translates graph variable references to table column references before applying standard optimization rules.

## Block Schema

### GraphScanStep Output

```
| Column Name      | Type    | Description              |
|------------------|---------|--------------------------|
| _graph_vertex_id | UInt64  | Vertex ID (key column)   |
| prop1            | Type1   | Mapped property 1        |
| prop2            | Type2   | Mapped property 2        |
| ...              | ...     | ...                      |
```

### GraphExpandStep Output

```
| Column Name        | Type    | Description              |
|--------------------|---------|--------------------------|
| _graph_src_id      | UInt64  | Source vertex ID          |
| _graph_dst_id      | UInt64  | Destination vertex ID     |
| _graph_edge_id     | UInt64  | Edge ID (key column)      |
| edge_prop1         | Type1   | Edge property 1           |
| edge_prop2         | Type2   | Edge property 2           |
| ...                | ...     | ...                       |
```

### GraphMultiHopExpandStep Output

```
| Column Name        | Type          | Description              |
|--------------------|---------------|--------------------------|
| _graph_start_id    | UInt64        | Start vertex ID          |
| _graph_end_id      | UInt64        | End vertex ID            |
| _graph_depth       | UInt32        | Path length (hops)       |
| _graph_path        | Array(UInt64) | Vertex IDs along path    |
```

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `max_graph_expansion_depth` | 100 | Maximum hops for variable-length path expansion |
| `max_graph_frontier_size` | 10000000 | Maximum frontier size before truncation |
| `graph_expand_batch_size` | 65536 | Batch size for IN-list queries during expand |
| `graph_track_visited` | true | Whether to track visited vertices (cycle prevention) |
