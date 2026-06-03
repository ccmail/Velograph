#pragma once

#include <Interpreters/Context_fwd.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>
#include <Processors/QueryPlan/ISourceStep.h>
#include <Storages/Graph/IGraphStorage_fwd.h>

namespace DB::Graph
{

/** GQL `MATCH` source step.
  *
  * Resolves its rows through an `IGraphStorage` when one is bound by the planner
  * (via `DatabaseCatalog`). Until a real graph storage is registered the bound
  * storage is null and the step falls back to an empty placeholder source, so the
  * plan shape stays runnable and produces zero rows.
  */
class MatchStep final : public ISourceStep
{
public:
    MatchStep(MatchSpec match_spec_, GraphStoragePtr graph_storage_, ContextPtr context_);

    String getName() const override { return "GraphMatch"; }

    QueryPlanStepPtr clone() const override;

    void initializePipeline(QueryPipelineBuilder & pipeline, const BuildQueryPipelineSettings &) override;

    const MatchSpec & getMatchSpec() const { return match_spec; }

private:
    static SharedHeader makeHeader(const MatchSpec & match_spec);

    MatchSpec match_spec;
    GraphStoragePtr graph_storage;
    ContextPtr context;
};

}
