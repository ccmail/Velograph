#pragma once

#include <Core/Block.h>
#include <Interpreters/Context_fwd.h>
#include <Storages/Graph/GraphSchemaRegistry.h>
#include <Storages/Graph/IGraphStorage.h>
#include <Storages/IStorage_fwd.h>

#include <memory>
#include <vector>

namespace DB
{

class Pipe;
class QueryPlan;
class StorageID;

/** Graph storage engine backed by MergeTree.
  *
  * For each graph, the engine manages four internal MergeTree tables:
  *
  *   <graph>.vertices        — vertex data, ORDER BY __ID__
  *   <graph>.edges_forward   — forward edges,  ORDER BY (__SRC__, type, __RANK__)
  *   <graph>.edges_reverse   — reverse edges,  ORDER BY (__DST__, type, __RANK__)
  *   <graph>.vertex_degrees  — out-degree counts, SummingMergeTree (maintained by MV)
  *
  * Writes go to both forward and reverse tables (2x storage amplification).
  * The degree table is maintained automatically via a materialized view.
  * Consistency is eventual: writes are not atomic across tables.
  *
  * As an `IGraphStorage` it is registered in `DatabaseCatalog` under the graph
  * database name and resolved by `resolveGraphStorage`; `IStorage::read` stays
  * fail-closed, while graph reads go through the traversal primitives
  * (`scan`, `getVertex`, `getNeighbors`).
  */
class GraphStorageEngine : public IGraphStorage
{
public:
    /// Column name constants used across all internal tables.
    static constexpr const char * COL_ID = "__ID__";
    static constexpr const char * COL_SRC = "__SRC__";
    static constexpr const char * COL_RANK = "__RANK__";
    static constexpr const char * COL_DST = "__DST__";
    static constexpr const char * COL_TYPE = "type";
    static constexpr const char * COL_LABELS = "labels";
    static constexpr const char * COL_OUT_DEGREE = "out_degree";

    /// Create a graph storage engine for the given graph database name.
    /// The database must already exist in DatabaseCatalog. The engine is registered
    /// as an `IStorage` under `{graph_name, "_graph"}` so it can be resolved through
    /// `DatabaseCatalog` by `resolveGraphStorage`.
    GraphStorageEngine(String graph_name_, ContextMutablePtr context_);

    /// --- IGraphStorage overrides ---

    std::string getName() const override { return "GraphStorage"; }

    /// TODO(graph-storage): back the traversal primitives by the internal tables.
    /// They stay fail-closed (inherited from `IGraphStorage`) until implemented.
    Pipe scan(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        GraphElementKind kind,
        size_t max_block_size,
        size_t num_streams) override;

    /// --- Schema management ---

    /// Register a vertex type in the schema and ensure the internal tables
    /// have the corresponding columns. Must be called before writing data.
    void registerVertexType(const String & type_name, const NamesAndTypes & columns);

    /// Register an edge type. Both source and target vertex types must already
    /// be registered.
    void registerEdgeType(const String & type_name, const NamesAndTypes & columns, const String & source_vertex_type,
                          const String & target_vertex_type);

    const GraphSchemaRegistry & getSchema() const { return schema; }

    /// --- Write operations ---

    /// Write a single vertex.
    /// `block` must contain __ID__, labels, and all property columns defined in the schema.
    void writeVertex(const Block & block);

    /// Write edges to both forward and reverse tables.
    /// `block` must contain __SRC__, __RANK__, __DST__, type, and all property columns.
    /// The reverse table write swaps __SRC__ / __DST__ ordering.
    void writeEdge(const Block & block);

    /// --- Read operations (low-level, used to back the primitives above) ---

    /// Read forward edges for a given source vertex and optional edge type.
    /// Returns a Pipe that streams the matching rows.
    /// If `edge_type` is empty, all edge types are returned.
    Pipe readForwardEdges(UInt64 src, const String & edge_type, ContextPtr context) const;

    /// Read reverse edges for a given destination vertex.
    Pipe readReverseEdges(UInt64 dst, const String & edge_type, ContextPtr context) const;

    /// Read a single vertex by ID. Returns an empty block if not found.
    Block readVertex(UInt64 id, ContextPtr context) const;

    /// Get the out-degree of a vertex for a given edge type.
    /// Returns 0 if the vertex has no edges of that type.
    UInt64 getDegree(UInt64 src, const String & edge_type, ContextPtr context) const;

    /// --- Table management ---

    /// Create all four internal MergeTree tables. Called once during graph creation.
    /// Idempotent: if tables already exist, does nothing.
    void createInternalTables();

    /// Drop all internal tables. Called during DROP GRAPH.
    void dropInternalTables();

    /// Check if internal tables have been created.
    bool isInitialized() const;

private:
    String graph_name;
    ContextMutablePtr context;
    GraphSchemaRegistry schema;

    /// Internal table names (fully qualified).
    String verticesTableName() const;
    String edgesForwardTableName() const;
    String edgesReverseTableName() const;
    String vertexDegreesTableName() const;

    /// Get the Storage object for an internal table. Throws if not found.
    StoragePtr getInternalStorage(const String & table_name) const;

    /// Build and execute a CREATE TABLE query for an internal MergeTree table.
    void createTable(const String & table_name, const NamesAndTypes & columns, const String & order_by, const String & engine,
                     bool is_materialized_view = false, const String & mv_source_table = {}, const String & mv_select = {});
};

}
