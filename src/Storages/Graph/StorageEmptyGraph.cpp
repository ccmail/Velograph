#include <Storages/Graph/StorageEmptyGraph.h>

#include <Processors/Sources/Graph/MatchSource.h>
#include <QueryPipeline/Pipe.h>

namespace DB
{

Pipe StorageEmptyGraph::scan(
    const SharedHeader & header,
    const IColumn::Filter & /*header_filter*/,
    GraphElementKind /*kind*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    /// No graph data is wired up yet: emit zero rows for the requested element kind.
    return Pipe(std::make_shared<Graph::MatchSource>(header, Graph::MatchSpec{}));
}

}
