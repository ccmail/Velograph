#include <Storages/Graph/IGraphStorage.h>

#include <Common/Exception.h>
#include <Processors/Sources/NullSource.h>
#include <QueryPipeline/Pipe.h>

#include <fmt/ranges.h>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
extern const int NOT_FOUND_COLUMN_IN_BLOCK;
}

namespace
{

/// Whether the projection requires swapping the src/dst columns, i.e. for a
/// `BACKWARD` traversal the output must be reported from the queried vertex's
/// source perspective.
bool needsSrcDstSwap(GraphDirection direction)
{
    return direction == GraphDirection::BACKWARD;
}

/// Locate the `__SRC__` and `__DST__` column positions in `header`. Returns
/// whether both are present; a header without them (e.g. a pure vertex table)
/// simply has nothing to swap.
bool findSrcDst(const Block & header, size_t & src_pos, size_t & dst_pos)
{
    bool has_src = false;
    bool has_dst = false;
    for (size_t i = 0; i < header.columns(); ++i)
    {
        const auto & name = header.getByPosition(i).name;
        if (name == GRAPH_COL_SRC)
        {
            src_pos = i;
            has_src = true;
        }
        else if (name == GRAPH_COL_DST)
        {
            dst_pos = i;
            has_dst = true;
        }
    }
    return has_src && has_dst;
}

/// Append one column of the full header to the projection, applying the
/// BACKWARD src/dst swap when requested. `header` is the full source header,
/// `i` is the column being appended.
void appendProjectionColumn(
    Block & projection,
    const Block & header,
    size_t i,
    size_t src_pos,
    size_t dst_pos,
    bool swap)
{
    const auto & col = header.getByPosition(i);
    if (swap)
    {
        if (i == src_pos)
            projection.insert({header.getByPosition(dst_pos).type, col.name});
        else if (i == dst_pos)
            projection.insert({header.getByPosition(src_pos).type, col.name});
        else
            projection.insert(col);
    }
    else
    {
        projection.insert(col);
    }
}

/// Backfill an empty pipe with a `NullSource` so downstream processors always
/// have a structure-correct input even when the storage produced no rows.
Pipe withNullSourceFallback(Pipe pipe, const SharedHeader & header)
{
    if (pipe.empty())
        return Pipe(std::make_shared<NullSource>(header));
    return pipe;
}

} // namespace

std::tuple<SharedHeader, IColumn::Filter>
IGraphStorage::getReturnHeaderForColumns(const NameSet & column_names, GraphElementKind kind, GraphDirection direction) const
{
    const Block & header = getGraphHeader(kind);
    IColumn::Filter return_header_filter(header.columns(), 0);

    Block projection;

    /// The src/dst swap only makes sense for edge projections; for vertices
    /// there is nothing to swap.
    const bool swap = (kind == GraphElementKind::Edge) && needsSrcDstSwap(direction);
    size_t src_pos = 0;
    size_t dst_pos = 0;
    if (swap)
    {
        if (!findSrcDst(header, src_pos, dst_pos))
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "BACKWARD projection requires both __SRC__ and __DST__ columns, but the header of {} has none",
                getName());
    }

    for (size_t i = 0; i < header.columns(); ++i)
    {
        if (column_names.count(header.getByPosition(i).name))
        {
            return_header_filter[i] = 1;
            appendProjectionColumn(projection, header, i, src_pos, dst_pos, swap);
        }
    }

    if (column_names.size() != projection.columns())
    {
        NameSet missing = column_names;
        for (const auto & col : projection)
            missing.erase(col.name);
        throw Exception(
            ErrorCodes::NOT_FOUND_COLUMN_IN_BLOCK,
            "Requested projection columns not found in the header of {}: {}",
            getName(),
            fmt::join(missing, ", "));
    }

    return {std::make_shared<const Block>(std::move(projection)), std::move(return_header_filter)};
}

SharedHeader IGraphStorage::getReturnHeader(const IColumn::Filter & header_filter, GraphElementKind kind, GraphDirection direction) const
{
    const Block & header = getGraphHeader(kind);
    if (header_filter.size() != header.columns())
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "header_filter size {} does not match the header column count {} of {}",
            header_filter.size(),
            header.columns(),
            getName());

    Block projection;

    const bool swap = (kind == GraphElementKind::Edge) && needsSrcDstSwap(direction);
    size_t src_pos = 0;
    size_t dst_pos = 0;
    if (swap)
    {
        if (!findSrcDst(header, src_pos, dst_pos))
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "BACKWARD projection requires both __SRC__ and __DST__ columns, but the header of {} has none",
                getName());
    }

    for (size_t i = 0; i < header_filter.size(); ++i)
    {
        if (header_filter[i])
            appendProjectionColumn(projection, header, i, src_pos, dst_pos, swap);
    }

    return std::make_shared<const Block>(std::move(projection));
}

// --- scan ---

Pipe IGraphStorage::scan(
    const NameSet & column_names,
    GraphElementKind kind,
    size_t max_block_size,
    size_t num_streams)
{
    auto [return_header, return_header_filter]
        = getReturnHeaderForColumns(column_names, kind, GraphDirection::FORWARD);
    return scan(return_header, return_header_filter, kind, max_block_size, num_streams);
}

Pipe IGraphStorage::scan(
    const SharedHeader & header,
    const IColumn::Filter & header_filter,
    GraphElementKind kind,
    size_t max_block_size,
    size_t num_streams)
{
    Pipe pipe = scanImpl(header, header_filter, kind, max_block_size, num_streams);
    return withNullSourceFallback(std::move(pipe), header);
}

// --- getVertex ---

Pipe IGraphStorage::getVertex(
    const NameSet & column_names,
    const Columns & id_columns,
    size_t max_block_size,
    size_t num_streams)
{
    auto [return_header, return_header_filter]
        = getReturnHeaderForColumns(column_names, GraphElementKind::Vertex, GraphDirection::FORWARD);
    return getVertex(return_header, return_header_filter, id_columns, max_block_size, num_streams);
}

Pipe IGraphStorage::getVertex(
    const SharedHeader & header,
    const IColumn::Filter & header_filter,
    const Columns & id_columns,
    size_t max_block_size,
    size_t num_streams)
{
    Pipe pipe = getVertexImpl(header, header_filter, id_columns, max_block_size, num_streams);
    return withNullSourceFallback(std::move(pipe), header);
}

// --- getEdge ---

Pipe IGraphStorage::getEdge(
    const NameSet & column_names,
    const Columns & edge_key_columns,
    size_t max_block_size,
    size_t num_streams)
{
    auto [return_header, return_header_filter]
        = getReturnHeaderForColumns(column_names, GraphElementKind::Edge, GraphDirection::FORWARD);
    return getEdge(return_header, return_header_filter, edge_key_columns, max_block_size, num_streams);
}

Pipe IGraphStorage::getEdge(
    const SharedHeader & header,
    const IColumn::Filter & header_filter,
    const Columns & edge_key_columns,
    size_t max_block_size,
    size_t num_streams)
{
    Pipe pipe = getEdgeImpl(header, header_filter, edge_key_columns, max_block_size, num_streams);
    return withNullSourceFallback(std::move(pipe), header);
}

// --- getNeighbors ---

Pipe IGraphStorage::getNeighbors(
    const NameSet & column_names,
    GraphDirection direction,
    const Columns & input_vertex_columns,
    size_t max_block_size,
    size_t num_streams,
    size_t limit_per_vertex,
    ASTPtr filter_pushdown)
{
    auto [return_header, return_header_filter]
        = getReturnHeaderForColumns(column_names, GraphElementKind::Edge, direction);
    return getNeighbors(
        return_header,
        return_header_filter,
        direction,
        input_vertex_columns,
        max_block_size,
        num_streams,
        limit_per_vertex,
        filter_pushdown);
}

Pipe IGraphStorage::getNeighbors(
    const SharedHeader & header,
    const IColumn::Filter & header_filter,
    GraphDirection direction,
    const Columns & input_vertex_columns,
    size_t max_block_size,
    size_t num_streams,
    size_t limit_per_vertex,
    ASTPtr filter_pushdown)
{
    Pipe pipe = getNeighborsImpl(
        header,
        header_filter,
        direction,
        input_vertex_columns,
        max_block_size,
        num_streams,
        limit_per_vertex,
        filter_pushdown);
    return withNullSourceFallback(std::move(pipe), header);
}

// --- fail-closed defaults ---

Pipe IGraphStorage::scanImpl(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    GraphElementKind /*kind*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage {} does not implement scan", getName());
}

Pipe IGraphStorage::getVertexImpl(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    const Columns & /*id_columns*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage {} does not implement getVertex", getName());
}

Pipe IGraphStorage::getEdgeImpl(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    const Columns & /*edge_key_columns*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage {} does not implement getEdge", getName());
}

Pipe IGraphStorage::getNeighborsImpl(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    GraphDirection /*direction*/,
    const Columns & /*input_vertex_columns*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/,
    size_t /*limit_per_vertex*/,
    ASTPtr /*filter_pushdown*/)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Graph storage {} does not implement getNeighbors", getName());
}

}
