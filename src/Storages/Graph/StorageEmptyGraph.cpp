#include <Storages/Graph/StorageEmptyGraph.h>

#include <Processors/Sources/Graph/MatchSource.h>
#include <QueryPipeline/Pipe.h>

namespace DB
{

Pipe StorageEmptyGraph::readGraphMatch(
    const Graph::MatchSpec & match_spec,
    const SharedHeader & header,
    ContextPtr /*context*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    /// No graph data is wired up yet: emit zero rows for the requested pattern.
    return Pipe(std::make_shared<Graph::MatchSource>(header, match_spec));
}

}
