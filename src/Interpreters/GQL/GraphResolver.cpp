#include <Interpreters/GQL/GraphResolver.h>

#include <Common/Exception.h>
#include <Interpreters/Context.h>
#include <Interpreters/DatabaseCatalog.h>
#include <Storages/Graph/IGraphStorage.h>

namespace DB {

namespace ErrorCodes {
extern const int NOT_IMPLEMENTED;
extern const int UNKNOWN_TABLE;
}  // namespace ErrorCodes

GraphStoragePtr resolveActiveGraphStorage(ContextPtr context) {
  const auto graph_name = context->getCurrentDatabase();
  auto storage = DatabaseCatalog::instance().tryGetTable({graph_name, "_graph"}, context);
  if (!storage)
    throw Exception(ErrorCodes::UNKNOWN_TABLE, "No active graph for MATCH: graph '{}' has no registered graph storage", graph_name);

  auto graph_storage = std::dynamic_pointer_cast<IGraphStorage>(storage);
  if (!graph_storage) throw Exception(ErrorCodes::UNKNOWN_TABLE, "Table {}._graph is not a graph storage", graph_name);

  return graph_storage;
}

GraphStoragePtr resolveActiveGraphStorage(const Graph::MatchSpec& spec, ContextPtr context) {
  if (spec.graph_reference) {
    /// M1: graph_reference resolution is not fully wired. The analyzer path
    /// does not populate graph_reference yet (USE-g / SELECT-FROM-g-MATCH
    /// scope propagation is a later milestone). A non-null value here means
    /// a form we cannot resolve yet.
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph reference resolution is not implemented in the analyzer planner yet");
  }

  return resolveActiveGraphStorage(context);
}

}  // namespace DB
