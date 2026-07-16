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
struct StorageID;

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
  * (`scan`, `getVertex`, `getEdge`, `getNeighbors`).
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
    bool supportScan() const override { return true; }

    /// --- Traversal primitive overrides ---
    ///
    /// Backed by direct calls to `MergeTreeDataSelectExecutor::read()` — no SQL
    /// is parsed or executed. Key-condition pushdown (mark skipping) is wired
    /// in progressively; the initial skeleton does full-scan with column
    /// projection to validate the end-to-end path.

    Pipe scanImpl(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        GraphElementKind kind,
        size_t max_block_size,
        size_t num_streams) override;

    Pipe getVertexImpl(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        const Columns & id_columns,
        size_t max_block_size,
        size_t num_streams) override;

    Pipe getEdgeImpl(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        const Columns & edge_key_columns,
        size_t max_block_size,
        size_t num_streams) override;

    Pipe getNeighborsImpl(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        GraphDirection direction,
        const Columns & input_vertex_columns,
        size_t max_block_size,
        size_t num_streams,
        size_t limit_per_vertex,
        ASTPtr filter_pushdown) override;

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

    /// --- Table management ---

    /// Create all four internal MergeTree tables. Called once during graph creation.
    /// Idempotent: if tables already exist, does nothing.
    void createInternalTables();

    /// Drop all internal tables. Called during DROP GRAPH.
    void dropInternalTables();

    /// Check if internal tables have been created.
    bool isInitialized() const;

protected:
    /// Lazily build and cache the vertex/edge internal headers from the schema.
    /// Used by the projection helpers in `IGraphStorage`.
    const Block & getGraphHeader(GraphElementKind kind) const override;

private:
    String graph_name;
    ContextMutablePtr context;
    GraphSchemaRegistry schema;

    /// Cached internal headers, built on first access.
    mutable Block vertex_header;
    mutable Block edge_header;

    /// Internal table names (fully qualified).
    String verticesTableName() const;
    String edgesForwardTableName() const;
    String edgesReverseTableName() const;
    String vertexDegreesTableName() const;

    /// Get the Storage object for an internal table. Throws if not found.
    StoragePtr getInternalStorage(const String & table_name) const;

    /// Read columns directly from an internal MergeTree table via
    /// `MergeTreeDataSelectExecutor::read()` — no SQL parsing, no
    /// `InterpreterSelectQuery`. The `SelectQueryInfo` is constructed
    /// in-place with an empty filter; key-condition pushdown is added
    /// progressively in the `xxxImpl` overrides.
    Pipe readFromInternalTable(
        const String & table_name,
        const Names & column_names,
        size_t max_block_size,
        size_t num_streams);

    /// Collect column names from a projection header (the subset requested
    /// by the caller after `IGraphStorage` column-mask resolution).
    static Names buildColumnNames(const SharedHeader & header);

    /// Build and execute a CREATE TABLE query for an internal MergeTree table.
    void createTable(const String & table_name, const NamesAndTypes & columns, const String & order_by, const String & engine,
                     bool is_materialized_view = false, const String & mv_source_table = {}, const String & mv_select = {});
};

}
