#include <Processors/Sources/Graph/MatchSource.h>

#include <utility>

namespace DB::Graph
{

MatchSource::MatchSource(SharedHeader header_, MatchSpec match_spec_)
    : ISource(std::move(header_))
    , match_spec(std::move(match_spec_))
{
}

Chunk MatchSource::generate()
{
    /// Real graph storage is not yet wired up: emit no rows.
    /// A future change will replace this source by a pipe built over `IStorage`
    /// resolved through `DatabaseCatalog` in `MatchStep::initializePipeline`.
    return {};
}

}
