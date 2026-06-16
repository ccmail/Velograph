#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Interpreters/Context_fwd.h>
#include <Interpreters/GQL/GQLPlanner.h>
#include <Interpreters/GQL/GQLQueryOptions.h>
#include <Interpreters/IInterpreter.h>
#include <Parsers/IAST_fwd.h>
#include <QueryPipeline/BlockIO.h>

namespace DB {

/** Interpreter for GQL queries using QueryTree-based analysis.
 *
 * This is the GQL counterpart of InterpreterSelectQueryAnalyzer:
 *
 * 1. Parser AST -> GQL QueryTree (GQLQueryTreeBuilder)
 * 2. QueryTree analysis passes (GQLQueryTreePassManager), choosing run vs runOnlyResolve
 *    from GQLQueryOptions, just like InterpreterSelectQueryAnalyzer
 * 3. QueryTree -> QueryPlan (GQL::Planner)
 * 4. QueryPlan -> QueryPipeline -> execution
 */
class InterpreterGQLQueryAnalyzer final : public IInterpreter {
 public:
  /** Construct from Parser AST: build the QueryTree and run analysis passes. */
  InterpreterGQLQueryAnalyzer(const ASTPtr& query_, const ContextPtr& context_, const GQLQueryOptions& gql_query_options_ = {});

  /** Construct from an already-built QueryTree (e.g. a nested query).
   * No query tree passes are applied.
   */
  InterpreterGQLQueryAnalyzer(const QueryTreeNodePtr& query_tree_, const ContextPtr& context_,
                              const GQLQueryOptions& gql_query_options_ = {});

  /** Execute the query and return the result pipeline. */
  BlockIO execute() override;

  /** Build the QueryPlan and return its output header without executing the pipeline. */
  SharedHeader getSampleBlock();

  /** Get the QueryTree for inspection or further processing. */
  QueryTreeNodePtr getQueryTree() const { return query_tree; }

  /** Return the QueryPlan, building it if necessary.
   * Mirrors InterpreterSelectQueryAnalyzer::getQueryPlan().
   */
  QueryPlan& getQueryPlan();

  /** Move out the QueryPlan, building it if necessary.
   * Mirrors InterpreterSelectQueryAnalyzer::extractQueryPlan().
   */
  QueryPlan&& extractQueryPlan() &&;

  bool ignoreQuota() const override { return gql_query_options.ignore_quota; }
  bool ignoreLimits() const override { return gql_query_options.ignore_limits; }

  const GQL::Planner& getPlanner() const { return planner; }
  GQL::Planner& getPlanner() { return planner; }

 private:
  ASTPtr query;
  ContextMutablePtr context;
  GQLQueryOptions gql_query_options;
  QueryTreeNodePtr query_tree;
  GQL::Planner planner;

  /** Parallel-replicas optimization builder.
   *
   * Currently commented out because GQL distributed execution / parallel replicas
   * support is not ready. The SQL analyzer uses this to generate an alternative plan
   * that reads from multiple replicas in parallel and then picks the cheaper plan.
   *
   * GQL MATCH queries read graph data; once they are backed by sharded MergeTree
   * tables and GQL::Planner can produce a standard QueryPlan that the optimizer
   * understands, this optimization can be enabled by mirroring
   * InterpreterSelectQueryAnalyzer::query_plan_with_parallel_replicas_builder.
   *
   * std::function<std::unique_ptr<QueryPlan>()> query_plan_with_parallel_replicas_builder;
   */
};


}  // namespace DB
