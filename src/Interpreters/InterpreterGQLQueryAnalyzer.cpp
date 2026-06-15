#include <Analyzer/GQL/GQLQueryTreeBuilder.h>
#include <Analyzer/GQL/Passes/GQLQueryTreePassManager.h>
#include <Common/Exception.h>
#include <Interpreters/ClientInfo.h>
#include <Interpreters/Context.h>
#include <Interpreters/GQL/GQLPlanner.h>
#include <Interpreters/InterpreterFactory.h>
#include <Interpreters/InterpreterGQLQueryAnalyzer.h>
#include <Interpreters/SelectQueryOptions.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#include <Processors/QueryPlan/Optimizations/QueryPlanOptimizationSettings.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <QueryPipeline/QueryPipelineBuilder.h>

namespace DB {

namespace ErrorCodes {
extern const int LOGICAL_ERROR;
}

namespace {

/** Build GQL QueryTree and run analysis passes.
 *
 * This is analogous to buildQueryTreeAndRunPasses() for SQL queries.
 * Currently, it only builds the QueryTree; analysis passes will be
 * added in a future step.
 */
QueryTreeNodePtr buildGQLQueryTreeAndRunPasses(const ASTPtr& query, const SelectQueryOptions& select_query_options,
                                               const ContextPtr& context) {
  if (!query) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL query AST is null");

  auto query_tree = GQL::buildGQLQueryTree(*query, context);

  GQL::GQLQueryTreePassManager pass_manager(context);
  GQL::GQLQueryTreePassManager::addDefaultPasses(pass_manager);

  /// Skip header-changing optimization passes for views, secondary (shard) queries, and
  /// callers that explicitly opt out of AST optimizations; otherwise run the full pipeline.
  /// Mirrors InterpreterSelectQueryAnalyzer's run vs runOnlyResolve choice.
  if (select_query_options.ignore_ast_optimizations || select_query_options.is_create_view ||
      context->getClientInfo().query_kind == ClientInfo::QueryKind::SECONDARY_QUERY)
    pass_manager.runOnlyResolve(query_tree);
  else
    pass_manager.run(query_tree);

  return query_tree;
}

}  // anonymous namespace

InterpreterGQLQueryAnalyzer::InterpreterGQLQueryAnalyzer(const ASTPtr& query_, const ContextPtr& context_,
                                                         const SelectQueryOptions& select_query_options_)
    : query(query_),
      context(context_),
      select_query_options(select_query_options_),
      query_tree(buildGQLQueryTreeAndRunPasses(query, select_query_options, context)) {}

InterpreterGQLQueryAnalyzer::InterpreterGQLQueryAnalyzer(const QueryTreeNodePtr& query_tree_, const ContextPtr& context_,
                                                         const SelectQueryOptions& select_query_options_)
    : query(nullptr), context(context_), select_query_options(select_query_options_), query_tree(query_tree_) {
  if (!query_tree) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL QueryTree is null");
}

BlockIO InterpreterGQLQueryAnalyzer::execute() {
  BlockIO result;
  QueryPlan query_plan;

  buildQueryPlan(query_plan);

  auto builder = query_plan.buildQueryPipeline(QueryPlanOptimizationSettings(context), BuildQueryPipelineSettings(context));
  result.pipeline = QueryPipelineBuilder::getPipeline(std::move(*builder));

  return result;
}

void InterpreterGQLQueryAnalyzer::buildQueryPlan(QueryPlan& query_plan) {
  if (!query_tree) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL QueryTree is null");

  /// Plan directly from the analyzed GQL QueryTree (no AST round-trip).
  GQL::buildGQLQueryPlan(query_plan, query_tree, context);
  query_plan.addInterpreterContext(context);
}

SharedHeader InterpreterGQLQueryAnalyzer::getSampleBlock() {
  QueryPlan query_plan;
  buildQueryPlan(query_plan);
  return query_plan.getCurrentHeader();
}

void registerInterpreterGQLQueryAnalyzer(InterpreterFactory& factory) {
  auto create_fn = [](const InterpreterFactory::Arguments& args) {
    return std::make_unique<InterpreterGQLQueryAnalyzer>(args.query, args.context, args.options);
  };
  factory.registerInterpreter("InterpreterGQLQueryAnalyzer", create_fn);
}

}  // namespace DB
