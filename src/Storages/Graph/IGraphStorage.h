#pragma once

#include <Columns/IColumn.h>
#include <Columns/IColumn_fwd.h>
#include <Core/Block_fwd.h>
#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>
#include <Storages/Graph/IGraphStorage_fwd.h>
#include <Storages/IStorage.h>

namespace DB
{

class Pipe;

/// Element type for the unified `scan` primitive.
enum class GraphElementKind
{
    Vertex,
    Edge,
};

/** Base interface for GQL graph storages.
  *
  * A graph storage is a regular ClickHouse `IStorage`, so it can be managed by
  * `DatabaseCatalog`, resolved through a `StorageID`, and reuse the storage
  * lifecycle and snapshot infrastructure, mirroring how `MergeTreeData` plugs into
  * the engine.
  *
  * Graph reading is exposed as a small set of traversal primitives (`scan`,
  * `getVertex`, `getNeighbors`) rather than the SQL-shaped `IStorage::read`:
  * GQL `MATCH` semantics live in `Graph::MatchSpec`, not in `SelectQueryInfo`.
  * Higher-level pattern matching is composed by the query engine from these
  * primitives, while `IStorage::read` stays fail-closed.
  *
  * Writing (for example GQL `INSERT` of nodes and edges) reuses `IStorage::write`,
  * which remains fail-closed by default until a concrete graph storage implements
  * it.
  */
class IGraphStorage : public IStorage
{
public:
    using IStorage::IStorage;

    /// --- Capability queries ---

    /// Whether full-scan traversal is supported.
    virtual bool supportScan() const { return false; }
    /// Whether in-place updates are supported.
    virtual bool supportUpdate() const { return false; }

    /// --- Lifecycle ---

    /// Called once the storage has been opened and is ready to serve reads/writes.
    virtual void onOpen() {}
    /// Called before the storage is closed and its resources released.
    virtual void onClose() {}

    /// --- Graph read primitives ---
    //
    // Each primitive returns a `Pipe` whose chunks match `header`. `header_filter`
    // is a row mask over the driving input (a value of `0` drops the corresponding
    // row); `id_columns` / `src_columns` carry the ids to look up, one row per
    // driving input row. Implementations override only the subset they support; the
    // defaults are fail-closed so a placeholder storage stays instantiable without
    // claiming capability it does not have.

    /// Scan all vertices or all edges, depending on `kind`.
    virtual Pipe scan(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        GraphElementKind kind,
        size_t max_block_size,
        size_t num_streams);

    /// Look up vertices by id. `id_columns` holds the ids to fetch (one column,
    /// one row per driving input row).
    virtual Pipe getVertex(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        const Columns & id_columns,
        size_t max_block_size,
        size_t num_streams);

    /// One-hop traversal: for each source id in `src_columns`, emit its neighbors
    /// along `direction`. `limit_per_node` caps the number of neighbors per source
    /// (0 means unlimited). `filter_pushdown` is an optional predicate pushed into
    /// the reader.
    virtual Pipe getNeighbors(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        Graph::MatchEdgeDirection direction,
        const Columns & src_columns,
        size_t max_block_size,
        size_t num_streams,
        size_t limit_per_node,
        ASTPtr filter_pushdown);
};

}
