#pragma once

#include <Core/Block.h>
#include <Storages/Graph/IGraphStorage.h>

namespace DB
{

/** A degenerate graph storage that always reports an empty graph.
  *
  * It is used as the default storage before a real graph is attached to a GQL
  * query (see `MatchStep`), so that read primitives keep compiling and return a
  * well-formed empty result instead of failing. Only `scanImpl` is overridden;
  * the other primitives inherit the fail-closed `NOT_IMPLEMENTED` defaults,
  * since an empty graph has no vertices/edges to look up.
  */
class StorageEmptyGraph final : public IGraphStorage
{
public:
    StorageEmptyGraph();

    String getName() const override { return "GraphEmpty"; }

protected:
    /// Always returns an empty header; an empty graph has no columns.
    const Block & getGraphHeader(GraphElementKind /*kind*/) const override;

    /// Full-scan over an empty graph produces no rows. The base shell backfills
    /// a `NullSource` matching the projection header, so callers still get a
    /// structure-correct empty pipe.
    Pipe scanImpl(
        const SharedHeader & /*header*/,
        const IColumn::Filter & /*header_filter*/,
        GraphElementKind /*kind*/,
        size_t /*max_block_size*/,
        size_t /*num_streams*/) override;

private:
    Block empty_header;
};

}
