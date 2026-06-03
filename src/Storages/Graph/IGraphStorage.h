#pragma once

#include <Core/Block_fwd.h>
#include <Interpreters/Context_fwd.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>
#include <Storages/Graph/IGraphStorage_fwd.h>
#include <Storages/IStorage.h>

namespace DB
{

class Pipe;

/** Base interface for GQL graph storages.
  *
  * A graph storage is a regular ClickHouse `IStorage`, so it can be managed by
  * `DatabaseCatalog`, resolved through a `StorageID`, and reuse the storage
  * lifecycle and snapshot infrastructure, mirroring how `MergeTreeData` plugs into
  * the engine.
  *
  * Graph reading deliberately does not reuse the SQL-shaped `IStorage::read`
  * signature: GQL `MATCH` semantics live in `Graph::MatchSpec`, not in
  * `SelectQueryInfo`. A graph source is produced through `readGraphMatch` instead,
  * while `IStorage::read` stays fail-closed.
  *
  * Writing (for example GQL `INSERT` of nodes and edges) reuses `IStorage::write`,
  * which remains fail-closed by default until a concrete graph storage implements
  * it.
  */
class IGraphStorage : public IStorage
{
public:
    using IStorage::IStorage;

    /** Build a source pipe that produces rows for a GQL `MATCH` described by
      * `match_spec`.
      *
      * `header` is the output header that `Graph::MatchStep` computes from the bound
      * pattern variables; implementations must produce chunks matching it.
      */
    virtual Pipe readGraphMatch(
        const Graph::MatchSpec & match_spec,
        const SharedHeader & header,
        ContextPtr context,
        size_t max_block_size,
        size_t num_streams)
        = 0;
};

}
