#pragma once

#include <Interpreters/Context_fwd.h>
#include <Parsers/graph/fwd_decl.h>

#include <string_view>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class PlanScope;

const OPENGQL::AST::GQLSingleQuery & getSingleQuerySubquery(
    const OPENGQL::AST::GQLSubquery & subquery,
    std::string_view context_name);

void bindSubqueryDefinitions(
    const OPENGQL::AST::GQLBindingVariableDefinitionBlock & block,
    PlanScope & scope,
    std::string_view context_name);

void planSubquerySource(
    QueryPlan & plan,
    const OPENGQL::AST::GQLSubquery & subquery,
    ContextPtr context,
    PlanScope & scope,
    PlanScope child_scope,
    std::string_view context_name);

void planPostSourceSubquery(
    QueryPlan & plan,
    const OPENGQL::AST::GQLSubquery & subquery,
    ContextPtr context,
    const PlanScope & outer_scope,
    PlanScope & subquery_scope,
    std::string_view context_name);

}
