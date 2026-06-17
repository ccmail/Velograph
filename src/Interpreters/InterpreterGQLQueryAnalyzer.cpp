#include <Analyzer/GQL/GQLQueryTreeBuilder.h>
#include <Analyzer/GQL/Passes/GQLQueryTreePassManager.h>
#include <Common/Exception.h>
#include <Core/Block.h>
#include <DataTypes/DataTypesNumber.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/ClientInfo.h>
#include <Interpreters/Context.h>
#include <Interpreters/GQL/GQLPlanner.h>
#include <Interpreters/GQL/GQLQueryOptions.h>
#include <Interpreters/InterpreterFactory.h>
#include <Interpreters/InterpreterGQLQueryAnalyzer.h>
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

ContextMutablePtr buildContext(const ContextPtr& context, const GQLQueryOptions& gql_query_options) {
  auto result_context = Context::createCopy(context);

  if (gql_query_options.shard_num)
    result_context->addSpecialScalar("_shard_num", Block{{DataTypeUInt32().createColumnConst(1, *gql_query_options.shard_num),
                                                          std::make_shared<DataTypeUInt32>(), "_shard_num"}});
  if (gql_query_options.shard_count)
    result_context->addSpecialScalar("_shard_count", Block{{DataTypeUInt32().createColumnConst(1, *gql_query_options.shard_count),
                                                            std::make_shared<DataTypeUInt32>(), "_shard_count"}});

  return result_context;
}

/** Build GQL QueryTree and run analysis passes.
 *
 * This is analogous to buildQueryTreeAndRunPasses() for SQL queries.
 */
QueryTreeNodePtr buildGQLQueryTreeAndRunPasses(const ASTPtr& query, const GQLQueryOptions& gql_query_options, const ContextPtr& context) {
  if (!query) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL query AST is null");

  auto query_tree = GQL::buildGQLQueryTree(*query, context);

  GQL::GQLQueryTreePassManager pass_manager(context);
  GQL::GQLQueryTreePassManager::addDefaultPasses(pass_manager);

  /// Skip header-changing optimization passes for views, secondary (shard) queries, and
  /// callers that explicitly opt out of AST optimizations; otherwise run the full pipeline.
  /// Mirrors InterpreterSelectQueryAnalyzer's run vs runOnlyResolve choice.
  if (gql_query_options.ignore_ast_optimizations || context->getClientInfo().query_kind == ClientInfo::QueryKind::SECONDARY_QUERY)
    pass_manager.runOnlyResolve(query_tree);
  else
    pass_manager.run(query_tree);

  return query_tree;
}

}  // anonymous namespace

InterpreterGQLQueryAnalyzer::InterpreterGQLQueryAnalyzer(const ASTPtr& query_, const ContextPtr& context_,
                                                         const GQLQueryOptions& gql_query_options_)
    : query(query_),
      context(buildContext(context_, gql_query_options_)),
      gql_query_options(gql_query_options_),
      query_tree(buildGQLQueryTreeAndRunPasses(query, gql_query_options, context)),
      planner(query_tree, context, gql_query_options) {}

InterpreterGQLQueryAnalyzer::InterpreterGQLQueryAnalyzer(const QueryTreeNodePtr& query_tree_, const ContextPtr& context_,
                                                         const GQLQueryOptions& gql_query_options_)
    : query(nullptr),
      context(buildContext(context_, gql_query_options_)),
      gql_query_options(gql_query_options_),
      query_tree(query_tree_),
      planner(query_tree, context, gql_query_options) {
  if (!query_tree) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL QueryTree is null");
}

BlockIO InterpreterGQLQueryAnalyzer::execute() {
  auto& plan = getQueryPlan();

  QueryPlanOptimizationSettings optimization_settings(context);
  BuildQueryPipelineSettings build_pipeline_settings(context);

  auto builder = plan.buildQueryPipeline(optimization_settings, build_pipeline_settings);

  BlockIO result;
  result.pipeline = QueryPipelineBuilder::getPipeline(std::move(*builder));

  if (!gql_query_options.ignore_quota) result.pipeline.setQuota(context->getQuota());

  return result;
}

QueryPlan& InterpreterGQLQueryAnalyzer::getQueryPlan() {
  planner.buildQueryPlanIfNeeded();
  return planner.getQueryPlan();
}

QueryPlan&& InterpreterGQLQueryAnalyzer::extractQueryPlan() && {
  planner.buildQueryPlanIfNeeded();
  return std::move(planner).extractQueryPlan();
}

SharedHeader InterpreterGQLQueryAnalyzer::getSampleBlock() { return getQueryPlan().getCurrentHeader(); }

void registerInterpreterGQLQueryAnalyzer(InterpreterFactory& factory) {
  auto create_fn = [](const InterpreterFactory::Arguments& args) {
    GQLQueryOptions gql_options;
    gql_options.only_analyze = args.options.only_analyze;
    gql_options.ignore_quota = args.options.ignore_quota;
    gql_options.ignore_limits = args.options.ignore_limits;
    gql_options.ignore_ast_optimizations = args.options.ignore_ast_optimizations;
    gql_options.is_internal = args.options.is_internal;
    gql_options.is_explain = args.options.is_explain;
    if (args.options.shard_num && args.options.shard_count) gql_options.setShardInfo(*args.options.shard_num, *args.options.shard_count);

    return std::make_unique<InterpreterGQLQueryAnalyzer>(args.query, args.context, gql_options);
  };
  factory.registerInterpreter("InterpreterGQLQueryAnalyzer", create_fn);
}

}  // namespace DB
