#include <Interpreters/GQL/MutationPlanner.h>

#include <Interpreters/GQL/PlanScope.h>
#include <Parsers/graph/GraphAST.h>
#include <Common/Exception.h>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace GQL
{
namespace
{

namespace GAST = DB::OPENGQL::AST;

const char * getDataModifyingClauseName(const IAST & clause)
{
    if (clause.as<GAST::GQLInsertClause>())
        return "INSERT";
    if (clause.as<GAST::GQLSetClause>())
        return "SET";
    if (clause.as<GAST::GQLRemoveClause>())
        return "REMOVE";
    if (clause.as<GAST::GQLDeleteClause>())
        return "DELETE";

    return nullptr;
}

bool isDataModifyingClause(const ASTPtr & clause)
{
    return clause && getDataModifyingClauseName(*clause) != nullptr;
}

void planDataModifyingClause(QueryPlan &, const ASTPtr & clause, ContextPtr, PlanScope &)
{
    if (!clause)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL data-modifying clause is null");

    const auto * name = getDataModifyingClauseName(*clause);
    if (!name)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL data-modifying planner received {}", clause->getID(' '));

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL {} execution is not supported", name);
}

}

bool tryPlanDataModifyingClause(QueryPlan & plan, const ASTPtr & clause, ContextPtr context, PlanScope & scope)
{
    if (!isDataModifyingClause(clause))
        return false;

    planDataModifyingClause(plan, clause, context, scope);
    return true;
}

}

}
