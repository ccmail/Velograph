#include <Storages/Graph/IGraphStorage.h>

#include <Common/Exception.h>
#include <QueryPipeline/Pipe.h>

namespace DB
{

namespace ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

/// Default graph read primitives are fail-closed: a storage that does not override
/// a primitive must not silently return empty data, because that would mask the
/// lack of support from the query layer. `IGraphStorage::read` stays fail-closed
/// through `IStorage`, and the primitives below fail closed so a placeholder
/// storage stays instantiable without claiming capability it does not have.

Pipe IGraphStorage::scan(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    GraphElementKind /*kind*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage does not implement scan");
}

Pipe IGraphStorage::getVertex(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    const Columns & /*id_columns*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage does not implement getVertex");
}

Pipe IGraphStorage::getNeighbors(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    Graph::MatchEdgeDirection /*direction*/,
    const Columns & /*src_columns*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/,
    size_t /*limit_per_node*/,
    ASTPtr /*filter_pushdown*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage does not implement getNeighbors");
}

}
