#include <Interpreters/InterpreterGQLQuery.h>

#include <Interpreters/Context.h>
#include <Interpreters/GQL/GQLPlanner.h>
#include <Interpreters/InterpreterFactory.h>
#include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#include <Processors/QueryPlan/Optimizations/QueryPlanOptimizationSettings.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <QueryPipeline/BlockIO.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Common/Exception.h>

#include <utility>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
}

InterpreterGQLQuery::InterpreterGQLQuery(ASTPtr query_ptr_, ContextPtr context_)
    : WithContext(context_)
    , query_ptr(std::move(query_ptr_))
{
}

BlockIO InterpreterGQLQuery::execute()
{
    BlockIO result;
    QueryPlan query_plan;

    buildQueryPlan(query_plan);

    auto builder = query_plan.buildQueryPipeline(QueryPlanOptimizationSettings(getContext()), BuildQueryPipelineSettings(getContext()));
    result.pipeline = QueryPipelineBuilder::getPipeline(std::move(*builder));

    return result;
}

void InterpreterGQLQuery::buildQueryPlan(QueryPlan & query_plan)
{
    if (!query_ptr)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "InterpreterGQLQuery query is null");

    GQL::buildGQLQueryPlan(query_plan, *query_ptr, getContext());
    query_plan.addInterpreterContext(getContext());
}

void registerInterpreterGQLQuery(InterpreterFactory & factory)
{
    auto create_fn = [](const InterpreterFactory::Arguments & args)
    {
        return std::make_unique<InterpreterGQLQuery>(args.query, args.context);
    };
    factory.registerInterpreter("InterpreterGQLQuery", create_fn);
}

}
