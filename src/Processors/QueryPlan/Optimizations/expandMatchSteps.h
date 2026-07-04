#pragma once

#include <Processors/QueryPlan/QueryPlan.h>

namespace DB::QueryPlanOptimizations
{

/// Expand logical MatchStep nodes into physical graph operator sub-trees.
///
/// This is a lowering pass, not an optimization: it runs unconditionally
/// (even when plan optimizations are disabled) so that the plan is always
/// in physical form before execution. For M1, only single-point patterns
/// `(n)` are expanded into a single MatchVertexStep; all other pattern
/// shapes throw NOT_IMPLEMENTED.
void expandMatchSteps(QueryPlan::Node & root, QueryPlan::Nodes & nodes);

}
