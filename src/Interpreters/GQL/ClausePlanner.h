#pragma once

#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>

#include <string_view>

namespace DB
{
class QueryPlan;
}

namespace DB::OPENGQL::AST
{
class GQLPageClause;
class GQLReturnClause;
class GQLSelectClause;
class GQLLetClause;
class GQLForClause;
class GQLFinishClause;
class GQLWhereClause;
}

namespace DB::GQL
{

class PlanScope;

/// Builds reusable GQL clauses that transform an existing `QueryPlan` stream.
void planWhereClause(
    QueryPlan & plan,
    const OPENGQL::AST::GQLWhereClause & where,
    ContextPtr context,
    std::string_view context_name,
    const PlanScope & scope);

void planReturnClause(
    QueryPlan & plan,
    const OPENGQL::AST::GQLReturnClause & ret,
    ContextPtr context,
    PlanScope & scope);

void planSelectClause(
    QueryPlan & plan,
    const OPENGQL::AST::GQLSelectClause & select,
    ContextPtr context,
    PlanScope & scope,
    bool source_was_planned = false);

void planProjectionItems(
    QueryPlan & plan,
    const ASTs & items,
    ContextPtr context,
    std::string_view context_name,
    PlanScope & scope);

void planLetClause(QueryPlan & plan, const OPENGQL::AST::GQLLetClause & let, ContextPtr context, PlanScope & scope);

void planForClause(QueryPlan & plan, const OPENGQL::AST::GQLForClause & for_clause, ContextPtr context, PlanScope & scope);

void planPageClause(QueryPlan & plan, const OPENGQL::AST::GQLPageClause & page, ContextPtr context, PlanScope & scope);

void planFinishClause(QueryPlan & plan, const OPENGQL::AST::GQLFinishClause & finish, PlanScope & scope);

void planClauseTransform(QueryPlan & plan, const ASTPtr & clause_ast, ContextPtr context, PlanScope & scope);

}
