#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Common/Logger.h>
#include <Interpreters/Context_fwd.h>
#include <Interpreters/GQL/GQLQueryOptions.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Parsers/IAST_fwd.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <QueryPipeline/StreamLocalLimits.h>

#include <memory>
#include <set>
#include <unordered_map>

namespace DB {
class GQLLinearQueryNode;
}

namespace DB::GQL {

class GQLPlannerContext;
using GQLPlannerContextPtr = std::shared_ptr<GQLPlannerContext>;

/** Planner for GQL QueryTree.
 *
 * This is the GQL counterpart of DB::Planner. It accepts an already analyzed GQL
 * QueryTree and lazily translates it into a QueryPlan.
 *
 * Interface shape mirrors DB::Planner:
 *   - buildQueryPlanIfNeeded() for lazy construction
 *   - getQueryPlan() / extractQueryPlan() for plan access
 *   - addStorageLimits, getUsedRowPolicies, getPlannerContext, getQueryNodeToPlanStepMapping
 *     are placeholders that return empty values until GQL grows the corresponding
 *     features (row policies, distributed contexts, parallel replicas).
 */
class Planner {
 public:
  /// Initialize planner with GQL query tree after analysis phase.
  Planner(const QueryTreeNodePtr &query_tree_, const ContextPtr &context_, const GQLQueryOptions &options_);

  /// Initialize planner with an externally supplied initial scope (used when the
  /// plan is part of a larger GQL query, e.g. nested subqueries).
  Planner(const QueryTreeNodePtr &query_tree_, const ContextPtr &context_, const GQLQueryOptions &options_, PlanScope &initial_scope_);

  const QueryPlan &getQueryPlan() const { return query_plan; }
  QueryPlan &getQueryPlan() { return query_plan; }

  const std::set<std::string> &getUsedRowPolicies() const { return used_row_policies; }

  void buildQueryPlanIfNeeded();

  QueryPlan &&extractQueryPlan() && { return std::move(query_plan); }

  void addStorageLimits(const StorageLimitsList &limits);

  GQLPlannerContextPtr getPlannerContext() const { return {}; }

  using QueryNodeToPlanStepMapping = std::unordered_map<const GQLLinearQueryNode *, const QueryPlan::Node *>;
  const QueryNodeToPlanStepMapping &getQueryNodeToPlanStepMapping() const { return query_node_to_plan_step_mapping; }

  ContextPtr getContext() const { return context; }
  const GQLQueryOptions &getOptions() const { return options; }
  const QueryTreeNodePtr &getQueryTree() const { return query_tree; }

 private:
  void buildPlanForLinearQueryNode();
  void buildPlanForCombinedQueryNode();

  LoggerPtr log = getLogger("GQLPlanner");
  QueryTreeNodePtr query_tree;
  ContextPtr context;
  GQLQueryOptions options;
  PlanScope plan_scope;
  QueryPlan query_plan;
  StorageLimitsList storage_limits;
  std::set<std::string> used_row_policies;
  QueryNodeToPlanStepMapping query_node_to_plan_step_mapping;
  bool query_plan_built = false;
};

// Legacy AST-based interface (will be deprecated)
void buildGQLQueryPlan(QueryPlan &query_plan, const IAST &query, ContextPtr context);

void buildGQLQueryPlan(QueryPlan &query_plan, const IAST &query, ContextPtr context, PlanScope &scope);

// QueryTree-based free-function interface (used by legacy planners and tests)
void buildGQLQueryPlan(QueryPlan &query_plan, const QueryTreeNodePtr &query_tree, ContextPtr context);

void buildGQLQueryPlan(QueryPlan &query_plan, const QueryTreeNodePtr &query_tree, ContextPtr context, PlanScope &scope);

}  // namespace DB::GQL
