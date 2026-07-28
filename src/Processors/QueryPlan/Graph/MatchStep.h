#pragma once

#include <Core/Names.h>
#include <Interpreters/Context_fwd.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>
#include <Processors/QueryPlan/ISourceStep.h>
#include <Storages/Graph/IGraphStorage_fwd.h>

namespace DB::Graph {

/** GQL `MATCH` logical source step.
 *
 * Carries the MatchSpec, the resolved graph storage, and the plan-layer
 * referenced column names. This step is purely logical: it must be replaced
 * by physical sub-operators (MatchVertexStep, MatchExpandStep, ...) by the
 * `expandMatchSteps` optimization pass before pipeline initialization.
 * Reaching `initializePipeline` means the expansion pass did not run or did
 * not cover the pattern shape, which is a logic error.
 */
class MatchStep final : public ISourceStep {
 public:
  MatchStep(MatchSpec match_spec_, GraphStoragePtr graph_storage_, Names referenced_columns_, ContextPtr context_);

  String getName() const override { return "GraphMatch"; }

  QueryPlanStepPtr clone() const override;

  /// Reaching here means `expandMatchSteps` did not replace this step.
  void initializePipeline(QueryPipelineBuilder &pipeline, const BuildQueryPipelineSettings &) override;

  const MatchSpec &getMatchSpec() const { return match_spec; }
  const GraphStoragePtr &getStorage() const { return graph_storage; }
  const Names &getReferencedColumns() const { return referenced_columns; }
  ContextPtr getContext() const { return context; }

 private:
  static SharedHeader makeHeader(const MatchSpec &match_spec, const GraphStoragePtr &graph_storage, const Names &referenced_columns);

  MatchSpec match_spec;
  GraphStoragePtr graph_storage;
  Names referenced_columns;
  ContextPtr context;
};

}  // namespace DB::Graph
