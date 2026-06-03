#include <Interpreters/GQL/SourcePlanner.h>

#include <Core/Block.h>
#include <Interpreters/GQL/ClausePlanner.h>
#include <Interpreters/GQL/MatchPlanner.h>
#include <Interpreters/GQL/PostSourceClausePlanner.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/GQL/SourceCompositionPlanner.h>
#include <Interpreters/GQL/SubqueryPlanner.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/Chunk.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Processors/QueryPlan/ReadFromPreparedSource.h>
#include <Processors/Sources/SourceFromSingleChunk.h>
#include <QueryPipeline/Pipe.h>

#include <memory>
#include <vector>

namespace DB::ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{
namespace
{

namespace GAST = DB::OPENGQL::AST;

void addEmptySingleRowSource(QueryPlan & plan, PlanScope & scope)
{
    auto header = std::make_shared<const Block>();
    Columns columns;
    auto source = std::make_shared<SourceFromSingleChunk>(header, Chunk(std::move(columns), 1));
    plan.addStep(std::make_unique<ReadFromPreparedSource>(Pipe(std::move(source))));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Source);
}

void planSourceFreeClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope)
{
    addEmptySingleRowSource(plan, scope);
    planPostSourceClause(plan, clause, context, scope);
}

void planSubquerySourceWithChildGraphScope(
    QueryPlan & plan,
    const GAST::GQLSubquery & subquery,
    ContextPtr context,
    PlanScope & scope,
    std::string_view context_name)
{
    planSubquerySource(plan, subquery, context, scope, scope.makeChildGraphScope(), context_name);
}

void planSelectSource(QueryPlan & plan, const ASTPtr & source, ContextPtr context, PlanScope & scope)
{
    if (!source)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "SELECT source is null");

    if (const auto * match = source->as<GAST::GQLMatchClause>())
    {
        std::vector<const GAST::GQLMatchClause *> matches{match};
        planMatchClauseSequence(plan, matches, context, scope);
        return;
    }

    if (const auto * subquery = source->as<GAST::GQLSubquery>())
    {
        planSubquerySourceWithChildGraphScope(plan, *subquery, context, scope, "SELECT FROM subquery");
        return;
    }

    if (const auto * source_item = source->as<GAST::GQLSelectSourceItem>())
    {
        if (source_item->graph_reference)
        {
            auto source_scope = scope.makeGraphOverrideScope(source_item->graph_reference);

            planSelectSource(plan, source_item->source, context, source_scope);

            scope.adoptBindingsAndKeepGraph(std::move(source_scope));
            return;
        }

        planSelectSource(plan, source_item->source, context, scope);
        return;
    }

    if (const auto * source_list = source->as<GAST::GQLSelectSourceList>())
    {
        if (source_list->items.empty())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM source list must contain at least one source");

        if (source_list->items.size() > 1)
        {
            planSelectSourceList(plan, *source_list, context, scope);
            return;
        }

        planSelectSource(plan, source_list->items.front(), context, scope);
        return;
    }

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported SELECT source: {}", source->getID(' '));
}

}

void planUseClause(const OPENGQL::AST::GQLUseClause & use, PlanScope & scope)
{
    if (!use.graph_reference)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL USE must contain a graph reference");

    scope.setActiveGraph(use.graph_reference->clone());
}

SourceClauseKind classifySourceIntroducingClause(const ASTPtr & clause)
{
    if (!clause)
        return SourceClauseKind::None;

    if (clause->as<GAST::GQLMatchClause>())
        return SourceClauseKind::Match;

    const auto * select = clause->as<GAST::GQLSelectClause>();
    if (select && select->source)
        return SourceClauseKind::SelectWithSource;

    return SourceClauseKind::None;
}

const char * sourceClauseKindName(SourceClauseKind kind)
{
    switch (kind)
    {
        case SourceClauseKind::None:
            return "none";
        case SourceClauseKind::Match:
            return "MATCH";
        case SourceClauseKind::SelectWithSource:
            return "SELECT FROM";
    }

    return "unknown";
}

bool isSourceIntroducingClause(const ASTPtr & clause)
{
    return classifySourceIntroducingClause(clause) != SourceClauseKind::None;
}

bool SourceClauseBuffer::tryAppend(const ASTPtr & clause)
{
    if (const auto * match = clause->as<OPENGQL::AST::GQLMatchClause>())
    {
        match_clauses.push_back(match);
        return true;
    }

    return false;
}

bool SourceClauseBuffer::hasPending() const
{
    return !match_clauses.empty();
}

void SourceClauseBuffer::flush(QueryPlan & plan, ContextPtr context, PlanScope & scope)
{
    if (match_clauses.empty())
        return;

    planMatchClauseSequence(plan, match_clauses, context, scope);
    match_clauses.clear();
}

bool tryPlanStandaloneSourceClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope)
{
    if (clause->as<GAST::GQLReturnClause>())
    {
        planSourceFreeClause(plan, clause, context, scope);
        return true;
    }

    if (const auto * select = clause->as<GAST::GQLSelectClause>())
    {
        if (select->source)
        {
            planSelectSource(plan, select->source, context, scope);
            planSelectClause(plan, *select, context, scope, true);
        }
        else
        {
            planSourceFreeClause(plan, clause, context, scope);
        }

        return true;
    }

    if (clause->as<GAST::GQLLetClause>())
    {
        planSourceFreeClause(plan, clause, context, scope);
        return true;
    }

    if (clause->as<GAST::GQLForClause>())
    {
        planSourceFreeClause(plan, clause, context, scope);
        return true;
    }

    if (clause->as<GAST::GQLFinishClause>())
    {
        planSourceFreeClause(plan, clause, context, scope);
        return true;
    }

    return false;
}

}
