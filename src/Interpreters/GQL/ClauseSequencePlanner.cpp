#include <Interpreters/GQL/ClauseSequencePlanner.h>

#include <Interpreters/GQL/CallPlanner.h>
#include <Interpreters/GQL/CatalogPlanner.h>
#include <Interpreters/GQL/MutationPlanner.h>
#include <Interpreters/GQL/PostSourceClausePlanner.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/GQL/SourcePlanner.h>
#include <Parsers/IAST.h>
#include <Parsers/graph/GraphAST.h>
#include <Common/Exception.h>

#include <utility>

namespace DB::ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{
namespace
{

class ClauseSequencePlannerState final
{
public:
    ClauseSequencePlannerState(QueryPlan & plan_, ContextPtr context_, PlanScope & scope_)
        : plan(plan_)
        , context(std::move(context_))
        , scope(scope_)
    {
    }

    void planClauses(const ASTs & clauses)
    {
        if (clauses.empty())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL query must contain at least one clause");

        for (const auto & clause : clauses)
            planClause(clause);

        if (source_buffer.hasPending())
            source_buffer.flush(plan, context, scope);
    }

private:
    void planClause(const ASTPtr & clause)
    {
        if (const auto * use = clause->as<OPENGQL::AST::GQLUseClause>())
        {
            planUseClauseBeforeSource(*use);
            return;
        }

        if (source_buffer.tryAppend(clause))
        {
            if (source_ready)
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL source clauses after post-source clauses are not supported");

            return;
        }

        flushPendingSource();

        if (!source_ready)
        {
            if (tryPlanClauseBeforeSource(clause))
                return;

            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL clause before a source clause is not supported: {}", clause->getID(' '));
        }

        planClauseAfterSource(clause);
    }

    void planUseClauseBeforeSource(const OPENGQL::AST::GQLUseClause & use)
    {
        if (source_ready || source_buffer.hasPending())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL USE after a source clause is not supported");

        planUseClause(use, scope);
    }

    void flushPendingSource()
    {
        if (!source_buffer.hasPending())
            return;

        source_buffer.flush(plan, context, scope);
        source_ready = true;
    }

    bool tryPlanClauseBeforeSource(const ASTPtr & clause)
    {
        if (tryPlanCatalogClause(plan, clause, context, scope))
        {
            source_ready = true;
            return true;
        }

        if (tryPlanDataModifyingClause(plan, clause, context, scope))
        {
            source_ready = true;
            return true;
        }

        if (tryPlanStandaloneCallClause(plan, clause, context, scope))
        {
            source_ready = true;
            return true;
        }

        if (tryPlanStandaloneSourceClause(plan, clause, context, scope))
        {
            source_ready = true;
            return true;
        }

        return false;
    }

    void planClauseAfterSource(const ASTPtr & clause)
    {
        if (isSourceIntroducingClause(clause))
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL source clauses after post-source clauses are not supported");

        planPostSourceClause(plan, clause, context, scope);
    }

    QueryPlan & plan;
    ContextPtr context;
    PlanScope & scope;
    SourceClauseBuffer source_buffer;
    bool source_ready = false;
};

}

void planClauseSequence(
    QueryPlan & plan,
    const ASTs & clauses,
    ContextPtr context,
    PlanScope & scope)
{
    ClauseSequencePlannerState(plan, std::move(context), scope).planClauses(clauses);
}

}
