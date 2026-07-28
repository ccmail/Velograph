#pragma once

#include <Interpreters/Context_fwd.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>
#include <Storages/Graph/IGraphStorage_fwd.h>

namespace DB
{

/// Resolve the graph storage registered for the context's current database.
/// All fail -> throw UNKNOWN_TABLE, never returns null.
GraphStoragePtr resolveActiveGraphStorage(ContextPtr context);

/// Resolve the active graph storage for a GQL MATCH query.
///
/// Resolution order:
///   1. MatchSpec.graph_reference (USE g / SELECT FROM g MATCH, if bound)
///   2. context current database: if that database has a GraphStorageEngine
///      registered as StorageID{db, "_graph"}, use it
/// All fail → throw UNKNOWN_TABLE, never returns null.
GraphStoragePtr resolveActiveGraphStorage(const Graph::MatchSpec & spec, ContextPtr context);

}
