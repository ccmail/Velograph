---
description: 'Graph Catalog: property graph definitions and table mapping'
sidebar_label: 'Graph Catalog'
sidebar_position: 84
slug: /development/graph/catalog
title: 'Graph Catalog and Table Mapping'
doc_type: 'reference'
---

# Graph Catalog and Table Mapping

This document describes how property graph definitions are managed and how they map to underlying ClickHouse tables.

## Overview

The Graph Catalog is a metadata layer that sits on top of `DatabaseCatalog`. It manages:

1. **Property graph definitions** - named graphs with vertex and edge table mappings.
2. **Vertex table mappings** - how a ClickHouse table represents vertices with labels and properties.
3. **Edge table mappings** - how a ClickHouse table represents edges with source/destination references.

The catalog does not create new storage engines. Vertex and edge tables are standard ClickHouse tables (`MergeTree`, `ReplacingMergeTree`, etc.).

## Data Model

### GraphDefinition

A property graph definition contains:

```cpp
struct GraphDefinition
{
    String graph_name;
    String database_name;
    UUID graph_uuid;

    std::vector<VertexTableMapping> vertex_tables;
    std::vector<EdgeTableMapping> edge_tables;

    /// Creation metadata
    time_t created_at;
    String created_by;
};
```

### VertexTableMapping

Maps a ClickHouse table to a vertex type in the graph:

```cpp
struct VertexTableMapping
{
    /// Underlying ClickHouse table
    String database_name;
    String table_name;

    /// Primary key column that serves as vertex ID
    String key_column;

    /// Graph label for this vertex type
    String label;

    /// Property mappings: graph_property_name -> table_column_name
    /// Allows renaming: PROPERTIES (user_id AS id, name, age)
    std::vector<PropertyMapping> properties;
};

struct PropertyMapping
{
    String source_column;    // Column name in ClickHouse table
    String graph_property;   // Property name in graph (may differ from column name)
};
```

### EdgeTableMapping

Maps a ClickHouse table to an edge type in the graph:

```cpp
struct EdgeTableMapping
{
    /// Underlying ClickHouse table
    String database_name;
    String table_name;

    /// Primary key column for the edge
    String key_column;

    /// Source vertex reference
    String source_key_column;         // Column in edge table
    String source_ref_table;          // Referenced vertex table
    String source_ref_column;         // Referenced column in vertex table

    /// Destination vertex reference
    String dest_key_column;           // Column in edge table
    String dest_ref_table;            // Referenced vertex table
    String dest_ref_column;           // Referenced column in vertex table

    /// Graph label for this edge type
    String label;

    /// Property mappings
    std::vector<PropertyMapping> properties;
};
```

## DDL Syntax

### CREATE PROPERTY GRAPH

```sql
CREATE PROPERTY GRAPH [IF NOT EXISTS] graph_name
  VERTEX TABLES (
    table_name KEY (key_column)
      [LABEL label_name]
      [PROPERTIES (col1 [AS alias1], col2 [AS alias2], ...)]
    [, ...]
  )
  EDGE TABLES (
    table_name KEY (key_column)
      SOURCE KEY (src_column) REFERENCES vertex_table (ref_column)
      DESTINATION KEY (dst_column) REFERENCES vertex_table (ref_column)
      [LABEL label_name]
      [PROPERTIES (col1 [AS alias1], col2 [AS alias2], ...)]
    [, ...]
  )
```

#### Semantics

- The referenced tables must already exist in ClickHouse.
- `KEY` columns must exist in the referenced table.
- `SOURCE`/`DESTINATION` references are logical (not enforced as foreign keys).
- If `LABEL` is omitted, the table name is used as the label.
- If `PROPERTIES` is omitted, all columns are exposed as graph properties.

#### Example

```sql
-- Assume these tables exist:
CREATE TABLE users (
    user_id UInt64,
    name String,
    age UInt32,
    city String
) ENGINE = MergeTree ORDER BY user_id;

CREATE TABLE follows (
    follow_id UInt64,
    follower_id UInt64,
    followee_id UInt64,
    created_at DateTime
) ENGINE = MergeTree ORDER BY (follower_id, followee_id);

CREATE TABLE companies (
    company_id UInt64,
    name String,
    industry String
) ENGINE = MergeTree ORDER BY company_id;

CREATE TABLE works_at (
    work_id UInt64,
    user_id UInt64,
    company_id UInt64,
    since Date,
    role String
) ENGINE = MergeTree ORDER BY (user_id, company_id);

-- Create the property graph
CREATE PROPERTY GRAPH social_network
  VERTEX TABLES (
    users KEY (user_id)
      LABEL Person
      PROPERTIES (user_id AS id, name, age),
    companies KEY (company_id)
      LABEL Company
      PROPERTIES (company_id AS id, name, industry)
  )
  EDGE TABLES (
    follows KEY (follow_id)
      SOURCE KEY (follower_id) REFERENCES users (user_id)
      DESTINATION KEY (followee_id) REFERENCES users (user_id)
      LABEL FOLLOWS
      PROPERTIES (created_at),
    works_at KEY (work_id)
      SOURCE KEY (user_id) REFERENCES users (user_id)
      DESTINATION KEY (company_id) REFERENCES companies (company_id)
      LABEL WORKS_AT
      PROPERTIES (since, role)
  );
```

### DROP PROPERTY GRAPH

```sql
DROP PROPERTY GRAPH [IF EXISTS] graph_name
```

This only removes the graph metadata. The underlying tables are not affected.

### ALTER PROPERTY GRAPH (future)

```sql
-- Add vertex/edge table mappings
ALTER PROPERTY GRAPH graph_name ADD VERTEX TABLE ...
ALTER PROPERTY GRAPH graph_name ADD EDGE TABLE ...

-- Remove vertex/edge table mappings
ALTER PROPERTY GRAPH graph_name DROP VERTEX TABLE table_name
ALTER PROPERTY GRAPH graph_name DROP EDGE TABLE table_name
```

## GraphCatalog Implementation

### Singleton Pattern

```cpp
class GraphCatalog : private boost::noncopyable
{
public:
    static GraphCatalog & instance();

    /// DDL operations
    void createGraph(const GraphDefinition & definition, bool if_not_exists);
    void dropGraph(const String & graph_name, bool if_exists);

    /// Lookup
    GraphDefinitionPtr getGraph(const String & graph_name) const;
    GraphDefinitionPtr tryGetGraph(const String & graph_name) const;
    std::vector<String> getAllGraphNames() const;

    /// Resolve mappings
    const VertexTableMapping * getVertexMapping(
        const String & graph_name, const String & label) const;
    const EdgeTableMapping * getEdgeMapping(
        const String & graph_name, const String & label) const;

    /// Find vertex mappings that reference a given table
    std::vector<const VertexTableMapping *> getVertexMappingsForTable(
        const String & graph_name,
        const String & database_name,
        const String & table_name) const;

    /// Persistence
    void loadFromDisk(const String & metadata_path);
    void saveToDisk(const String & metadata_path) const;

private:
    mutable std::shared_mutex mutex;
    std::unordered_map<String, GraphDefinitionPtr> graphs;
};
```

### Metadata Persistence

Graph definitions are persisted as SQL files in the metadata directory, following the pattern used by ClickHouse for table metadata:

```
metadata/
  graphs/
    social_network.graph.sql    -- Contains the CREATE PROPERTY GRAPH statement
    knowledge_graph.graph.sql
```

On server startup, `GraphCatalog::loadFromDisk` reads all `.graph.sql` files and reconstructs the in-memory catalog.

### Validation

When creating a graph, the interpreter validates:

1. All referenced tables exist in `DatabaseCatalog`.
2. All referenced columns exist in the table schema.
3. KEY columns have appropriate types (integer or string).
4. SOURCE/DESTINATION references point to valid vertex tables within the same graph.
5. Labels are unique within the graph (no two vertex tables share a label).

## System Tables

Three system tables expose graph metadata for introspection:

### system.graphs

| Column | Type | Description |
|--------|------|-------------|
| `name` | String | Graph name |
| `database` | String | Database containing the graph |
| `uuid` | UUID | Graph UUID |
| `vertex_table_count` | UInt32 | Number of vertex table mappings |
| `edge_table_count` | UInt32 | Number of edge table mappings |
| `created_at` | DateTime | Creation timestamp |

### system.graph_vertices

| Column | Type | Description |
|--------|------|-------------|
| `graph` | String | Graph name |
| `label` | String | Vertex label |
| `table_database` | String | Underlying table database |
| `table_name` | String | Underlying table name |
| `key_column` | String | Primary key column |
| `properties` | Array(Tuple(String, String)) | [(graph_prop, table_col), ...] |

### system.graph_edges

| Column | Type | Description |
|--------|------|-------------|
| `graph` | String | Graph name |
| `label` | String | Edge label |
| `table_database` | String | Underlying table database |
| `table_name` | String | Underlying table name |
| `key_column` | String | Edge key column |
| `source_key` | String | Source key column in edge table |
| `source_ref_table` | String | Referenced vertex table |
| `source_ref_column` | String | Referenced column |
| `dest_key` | String | Destination key column |
| `dest_ref_table` | String | Referenced vertex table |
| `dest_ref_column` | String | Referenced column |
| `properties` | Array(Tuple(String, String)) | [(graph_prop, table_col), ...] |

### Implementation Pattern

System tables follow the `IStorageSystemOneBlock` pattern:

```cpp
class StorageSystemGraphs final : public IStorageSystemOneBlock
{
public:
    std::string getName() const override { return "SystemGraphs"; }
    static ColumnsDescription getColumnsDescription();

protected:
    void fillData(
        MutableColumns & res_columns,
        ContextPtr context,
        const ActionsDAG::Node * predicate,
        std::vector<UInt8> columns_mask) const override
    {
        for (const auto & graph_name : GraphCatalog::instance().getAllGraphNames())
        {
            auto graph = GraphCatalog::instance().getGraph(graph_name);
            // Fill res_columns with graph metadata
        }
    }
};
```

## Label Resolution

During query interpretation, labels in GQL patterns are resolved to table mappings:

```
GQL: MATCH (a:Person)-[:FOLLOWS]->(b:Company)
                |           |           |
                v           v           v
      getVertexMapping    getEdgeMapping  getVertexMapping
      ("social", "Person") ("social",     ("social", "Company")
                            "FOLLOWS")
                |           |           |
                v           v           v
         VertexMapping   EdgeMapping   VertexMapping
         {table: users}  {table:       {table: companies}
                          follows}
```

If a pattern has no label (e.g., `(a)`), all vertex tables in the graph are candidates, and the interpreter generates a UNION of scans.

## Interaction with DatabaseCatalog

`GraphCatalog` depends on `DatabaseCatalog` for table resolution:

```cpp
void InterpreterCreateGraph::execute()
{
    auto & db_catalog = DatabaseCatalog::instance();

    for (const auto & vtx : create.vertex_tables)
    {
        // Verify the underlying table exists
        auto storage = db_catalog.getTable(
            StorageID(vtx.database_name, vtx.table_name), context);

        // Verify columns exist
        auto metadata = storage->getInMemoryMetadataPtr();
        // ... validate key_column, properties ...
    }

    // Register in GraphCatalog
    GraphCatalog::instance().createGraph(definition, create.if_not_exists);
}
```

## Edge Table Design Recommendations

For optimal graph query performance, edge tables should be ordered to support efficient expansion:

```sql
-- For outgoing edge expansion (most common):
-- ORDER BY source column first
CREATE TABLE follows (
    follow_id UInt64,
    follower_id UInt64,
    followee_id UInt64,
    created_at DateTime
) ENGINE = MergeTree
ORDER BY (follower_id, followee_id);

-- For bidirectional expansion:
-- Consider a materialized view with reverse ordering
CREATE MATERIALIZED VIEW follows_reverse
ENGINE = MergeTree ORDER BY (followee_id, follower_id)
AS SELECT * FROM follows;
```

The `GraphExpandStep` can use `follows_reverse` for incoming edge expansion, avoiding full table scans.
