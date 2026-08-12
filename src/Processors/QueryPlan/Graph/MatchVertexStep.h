#pragma once

#include <Core/Names.h>
#include <Interpreters/Context_fwd.h>
#include <Processors/QueryPlan/SourceStepWithFilter.h>
#include <Storages/Graph/IGraphStorage_fwd.h>

namespace DB::Graph
{

/** Physical leaf source for a single graph vertex variable.
  *
  * Produced by `expandMatchSteps` from a single-point `MatchStep` (pattern `(n)`).
  * Inherits `SourceStepWithFilterBase` so that existing optimization passes
  * (`tryPushDownFilter`, `optimizePrimaryKeyConditionAndLimit`) can push
  * predicates into the storage scan for free.
  *
  * M2: predicates still execute in the `FilterStep` above this source. The output
  * includes the variable id and every eagerly referenced property column.
  */
class MatchVertexStep final : public SourceStepWithFilterBase
{
public:
    MatchVertexStep(
        GraphStoragePtr storage_,
        String variable_,
        Names referenced_columns_,
        ContextPtr context_);

    String getName() const override { return "MatchVertex"; }

    QueryPlanStepPtr clone() const override;

    void initializePipeline(QueryPipelineBuilder & pipeline, const BuildQueryPipelineSettings &) override;

    void describeActions(FormatSettings & format_settings) const override;
    void describeActions(JSONBuilder::JSONMap & map) const override;

private:
    static SharedHeader makeHeader(
        const GraphStoragePtr & storage,
        const String & variable,
        const Names & referenced_columns);

    GraphStoragePtr storage;
    String variable;
    Names referenced_columns;
    ContextPtr context;
};

}
