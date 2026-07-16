#include <Storages/Graph/StorageEmptyGraph.h>

#include <QueryPipeline/Pipe.h>

namespace DB
{

StorageEmptyGraph::StorageEmptyGraph()
    : IGraphStorage(StorageID("default", "__graph_empty"))
{
}

const Block & StorageEmptyGraph::getGraphHeader(GraphElementKind /*kind*/) const
{
    return empty_header;
}

Pipe StorageEmptyGraph::scanImpl(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    GraphElementKind /*kind*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    /// An empty graph has nothing to scan. Returning an empty pipe is correct;
    /// the base `scan` shell turns it into a `NullSource` matching the
    /// projection header.
    return {};
}

}
