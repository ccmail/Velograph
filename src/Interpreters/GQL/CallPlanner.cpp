#include <Interpreters/GQL/CallPlanner.h>

#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/GQL/SubqueryPlanner.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Common/Exception.h>

#include <unordered_set>
#include <utility>

namespace DB::ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{
namespace
{

namespace GAST = DB::OPENGQL::AST;

Names extractInlineCallVariableScope(const GAST::GQLCallInlineClause & call)
{
    Names imports;
    if (!call.variable_scope)
        return imports;

    const auto * variable_scope = call.variable_scope->as<GAST::GQLCallVariableScopeClause>();
    if (!variable_scope)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "inline CALL variable scope must be GQLCallVariableScopeClause");

    std::unordered_set<String> seen;
    imports.reserve(variable_scope->variables.size());
    for (const auto & variable_ast : variable_scope->variables)
    {
        const auto * variable = variable_ast ? variable_ast->as<GAST::GQLExpr>() : nullptr;
        if (!variable || variable->kind != GAST::GQLExpr::Kind::Identifier || variable->text.empty())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "inline CALL variable scope must contain binding-variable identifiers");
        if (!seen.insert(variable->text).second)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Duplicate inline CALL variable scope import '{}'", variable->text);

        imports.push_back(variable->text);
    }

    return imports;
}

PlanScope makeInlineCallSourceScope(const PlanScope & scope, const Names & imports)
{
    auto child_scope = scope.makeChildGraphScope();
    for (const auto & name : imports)
    {
        const auto * binding = scope.tryGetBinding(name);
        if (!binding)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "inline CALL variable scope binding '{}' is not available", name);
        if (!binding->expression)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "inline CALL variable scope binding '{}' requires correlation semantics", name);

        child_scope.addOrReplaceBinding(name, binding->type, binding->kind, binding->expression->clone());
    }

    return child_scope;
}

PlanScope makeInlineCallPostSourceScope(const PlanScope & scope, const Names & imports)
{
    auto child_scope = scope.makeChildGraphScope();
    for (const auto & name : imports)
    {
        const auto * binding = scope.tryGetBinding(name);
        if (!binding)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "inline CALL variable scope binding '{}' is not available", name);

        child_scope.addOrReplaceBinding(
            name,
            binding->type,
            binding->kind,
            binding->expression ? binding->expression->clone() : ASTPtr{});
    }

    return child_scope;
}

const GAST::GQLSubquery & getInlineCallSubquery(const GAST::GQLCallInlineClause & call)
{
    if (call.optional)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "OPTIONAL inline CALL is not supported");

    const auto * subquery = call.subquery ? call.subquery->as<GAST::GQLSubquery>() : nullptr;
    if (!subquery)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "inline CALL must contain a subquery");

    return *subquery;
}

bool isNamedCallClause(const ASTPtr & clause)
{
    return clause && clause->as<GAST::GQLCallNamedClause>();
}

void planNamedCallClause(QueryPlan &, const ASTPtr & clause, ContextPtr, PlanScope &)
{
    if (!isNamedCallClause(clause))
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL procedure clause must be GQLCallNamedClause");

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL named CALL execution is not supported");
}

void planInlineCallSource(
    QueryPlan & plan,
    const OPENGQL::AST::GQLCallInlineClause & call,
    ContextPtr context,
    PlanScope & scope)
{
    const auto & subquery = getInlineCallSubquery(call);
    planSubquerySource(
        plan,
        subquery,
        context,
        scope,
        makeInlineCallSourceScope(scope, extractInlineCallVariableScope(call)),
        "inline CALL subquery");
}

void planInlineCallPostSourceClause(
    QueryPlan & plan,
    const OPENGQL::AST::GQLCallInlineClause & call,
    ContextPtr context,
    PlanScope & scope)
{
    const auto & subquery = getInlineCallSubquery(call);
    auto child_scope = makeInlineCallPostSourceScope(scope, extractInlineCallVariableScope(call));
    planPostSourceSubquery(plan, subquery, context, scope, child_scope, "post-source inline CALL subquery");
    scope = std::move(child_scope);
}

}

bool tryPlanStandaloneCallClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope)
{
    if (const auto * inline_call = clause ? clause->as<GAST::GQLCallInlineClause>() : nullptr)
    {
        planInlineCallSource(plan, *inline_call, context, scope);
        return true;
    }

    if (isNamedCallClause(clause))
    {
        planNamedCallClause(plan, clause, context, scope);
        return true;
    }

    return false;
}

bool tryPlanPostSourceCallClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope)
{
    if (const auto * inline_call = clause ? clause->as<GAST::GQLCallInlineClause>() : nullptr)
    {
        planInlineCallPostSourceClause(plan, *inline_call, context, scope);
        return true;
    }

    if (isNamedCallClause(clause))
    {
        planNamedCallClause(plan, clause, context, scope);
        return true;
    }

    return false;
}

}
