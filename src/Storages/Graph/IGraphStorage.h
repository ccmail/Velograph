#pragma once

#include <Columns/IColumn.h>
#include <Columns/IColumn_fwd.h>
#include <Core/Block.h>
#include <Core/Block_fwd.h>
#include <Core/Names.h>
#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>
#include <Storages/Graph/IGraphStorage_fwd.h>
#include <Storages/IStorage.h>

namespace DB {

class Pipe;

/// Element type for the `scan` primitive.
enum class GraphElementKind {
  Vertex,
  Edge,
};

/// Traversal direction for graph read primitives. It covers the full GQL
/// direction semantics; GQL-layer composite directions (such as
/// `IncomingOrOutgoing`) are lowered into one or more calls with these
/// physical directions by the query plan layer, not by the storage layer.
///
/// The current roadmap only models a single edge storage type — the directed
/// edge table — where every edge is written to both `edges_forward` and its
/// `edges_reverse` mirror. An undirected GQL match (`-[e]-`) is served by the
/// `UNDIRECTED` direction reading both tables and unioning the output, rather
/// than by a dedicated undirected edge table. No fourth direction is added,
/// and no `Bidirectional` value is introduced: in the dual-write model
/// `UNDIRECTED` already covers bidirectional access, so a separate value would
/// be redundant.
enum class GraphDirection : UInt8 {
  FORWARD,     ///< Follow src -> dst along `edges_forward` (keyed by `__SRC__`).
  BACKWARD,    ///< Follow dst -> src along `edges_reverse` (keyed by `__DST__`);
               ///<  the output swaps src/dst so the result is always reported
               ///<  from the queried vertex's source perspective.
  UNDIRECTED,  ///< Ignore direction; for a directed edge table this reads both
               ///<  `edges_forward` and `edges_reverse` and unions the output.
               ///<  Whether the union is deduplicated is declared by the caller.
};

/// Column name used internally for the edge source. Identifying these by name
/// (instead of by a fixed column index) keeps the BACKWARD src/dst swap robust
/// to changes in the internal table layout.
inline static const char* const GRAPH_COL_ID = "__ID__";
inline static const char* const GRAPH_COL_SRC = "__SRC__";
inline static const char* const GRAPH_COL_DST = "__DST__";

/** Base interface for GQL graph storages.
 *
 * A graph storage is a regular ClickHouse `IStorage`, so it can be managed by
 * `DatabaseCatalog`, resolved through a `StorageID`, and reuse the storage
 * lifecycle and snapshot infrastructure, mirroring how `MergeTreeData` plugs
 * into the engine.
 *
 * Graph reading is exposed as a small set of traversal primitives (`scan`,
 * `getVertex`, `getEdge`, `getNeighbors`) rather than the SQL-shaped
 * `IStorage::read`: GQL `MATCH` semantics live in `Graph::MatchSpec`, not in
 * `SelectQueryInfo`. Higher-level pattern matching is composed by the query
 * engine from these primitives, while `IStorage::read` stays fail-closed.
 *
 * Each primitive is layered in three stages:
 *   1. `NameSet` overload — convenience entry point accepting projection
 *      column names; the base class converts them into a column mask.
 *   2. `SharedHeader` + `IColumn::Filter` overload — the final form, where the
 *      filter is a *column* mask (length == number of columns) carrying
 *      projection pushdown: `header_filter[i] == 0` means the i-th column is
 *      not returned. Row-level filtering goes through the separate
 *      `filter_pushdown` AST parameter and is never mixed into this mask.
 *   3. `xxxImpl` virtual — the subclass override point. The base shell does
 *      direction normalization, swaps src/dst columns for `BACKWARD`,
 *      backfills a `NullSource` for empty results, and validates the element
 *      kind, so subclasses only implement the actual read.
 *
 * Writing (for example GQL `INSERT` of nodes and edges) reuses
 * `IStorage::write`, which remains fail-closed by default until a concrete
 * graph storage implements it.
 */
class IGraphStorage : public IStorage {
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

  /// Full internal vertex or edge header used for analyzer type resolution
  /// and storage projection.
  virtual const Block& getGraphHeader(GraphElementKind kind) const = 0;
  /// Called before the storage is closed and its resources released.
  virtual void onClose() {}

  /// --- scan: three-stage layering ---
  //
  /// Scan all vertices or all edges, depending on `kind`. Stage 1 accepts the
  /// projection column names; stage 2 accepts the column mask directly.

  Pipe scan(const NameSet& column_names, GraphElementKind kind, size_t max_block_size, size_t num_streams);

  Pipe scan(const SharedHeader& header, const IColumn::Filter& header_filter, GraphElementKind kind, size_t max_block_size,
            size_t num_streams);

  /// --- getVertex: three-stage layering ---
  //
  /// Look up vertices by id. `id_columns` holds the ids to fetch (one column,
  /// one row per driving input row).

  Pipe getVertex(const NameSet& column_names, const Columns& id_columns, size_t max_block_size, size_t num_streams);

  Pipe getVertex(const SharedHeader& header, const IColumn::Filter& header_filter, const Columns& id_columns, size_t max_block_size,
                 size_t num_streams);

  /// --- getEdge: three-stage layering ---
  //
  /// Look up edges by edge key. The key may be a stable edge id or a composite
  /// key (src/type/dst/...); the storage decides which lookup path to use
  /// internally, so a single primitive replaces separate primary/unique index
  /// entry points.

  Pipe getEdge(const NameSet& column_names, const Columns& edge_key_columns, size_t max_block_size, size_t num_streams);

  Pipe getEdge(const SharedHeader& header, const IColumn::Filter& header_filter, const Columns& edge_key_columns, size_t max_block_size,
               size_t num_streams);

  /// --- getNeighbors: three-stage layering ---
  //
  /// One-hop traversal: for each source id in `input_vertex_columns`, emit its
  /// neighbors along `direction`. `limit_per_vertex` caps the number of
  /// neighbors per source (0 means unlimited). `filter_pushdown` is an
  /// optional row-level predicate pushed into the reader; it is never mixed
  /// into the column-mask `header_filter`.

  Pipe getNeighbors(const NameSet& column_names, GraphDirection direction, const Columns& input_vertex_columns, size_t max_block_size,
                    size_t num_streams, size_t limit_per_vertex, ASTPtr filter_pushdown);

  Pipe getNeighbors(const SharedHeader& header, const IColumn::Filter& header_filter, GraphDirection direction,
                    const Columns& input_vertex_columns, size_t max_block_size, size_t num_streams, size_t limit_per_vertex,
                    ASTPtr filter_pushdown);

 protected:
  /// --- Subclass override points (fail-closed defaults) ---

  virtual Pipe scanImpl(const SharedHeader& header, const IColumn::Filter& header_filter, GraphElementKind kind, size_t max_block_size,
                        size_t num_streams);

  virtual Pipe getVertexImpl(const SharedHeader& header, const IColumn::Filter& header_filter, const Columns& id_columns,
                             size_t max_block_size, size_t num_streams);

  virtual Pipe getEdgeImpl(const SharedHeader& header, const IColumn::Filter& header_filter, const Columns& edge_key_columns,
                           size_t max_block_size, size_t num_streams);

  virtual Pipe getNeighborsImpl(const SharedHeader& header, const IColumn::Filter& header_filter, GraphDirection direction,
                                const Columns& input_vertex_columns, size_t max_block_size, size_t num_streams, size_t limit_per_vertex,
                                ASTPtr filter_pushdown);

  /// Build the projection header and its column mask from a set of requested
  /// column names. `kind` selects the vertex or edge internal header; for an
  /// edge projection, `BACKWARD` swaps the src/dst columns so callers always
  /// see the result from the queried vertex's source perspective (vertex
  /// projections never swap, so `direction` is ignored for them).
  std::tuple<SharedHeader, IColumn::Filter> getReturnHeaderForColumns(const NameSet& column_names, GraphElementKind kind,
                                                                      GraphDirection direction) const;

  /// Build the projection header from an existing column mask. `kind` selects
  /// the header, `direction` controls the edge src/dst swap (ignored for
  /// vertices).
  SharedHeader getReturnHeader(const IColumn::Filter& header_filter, GraphElementKind kind, GraphDirection direction) const;
};

}  // namespace DB
