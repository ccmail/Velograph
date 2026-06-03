#pragma once

#include <Processors/ISource.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>

namespace DB::Graph
{

/// A placeholder source for GQL `MATCH` until graph data is exposed through `IStorage`.
///
/// Produces zero rows. The eventual replacement will not be a bespoke source at all:
/// `MatchStep::initializePipeline` will build a `ReadFromMergeTree`-style pipe over
/// the underlying tables resolved through `DatabaseCatalog`.
class MatchSource final : public ISource
{
public:
    MatchSource(SharedHeader header_, MatchSpec match_spec_);

    String getName() const override { return "GraphMatchSource"; }

protected:
    Chunk generate() override;

private:
    MatchSpec match_spec;
};

}
