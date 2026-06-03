#include <Interpreters/GQL/SourceCompositionPlanner.h>

#include <Interpreters/GQL/MatchPlanner.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/GQL/SourcePlanner.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Common/Exception.h>

#include <utility>
#include <vector>

namespace DB::ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{
namespace
{

namespace GAST = DB::OPENGQL::AST;

bool sameGraphReference(const IAST & lhs_ast, const IAST & rhs_ast)
{
    const auto * lhs = lhs_ast.as<GAST::GQLGraphExpression>();
    const auto * rhs = rhs_ast.as<GAST::GQLGraphExpression>();
    if (!lhs || !rhs)
        return false;

    if (lhs->kind != rhs->kind || lhs->text != rhs->text)
        return false;

    return !lhs->value && !rhs->value;
}

SelectSourceListEntry classifySourceListEntry(const ASTPtr & item_ast)
{
    const auto * item = item_ast ? item_ast->as<GAST::GQLSelectSourceItem>() : nullptr;
    if (!item)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM source list item must be GQLSelectSourceItem");
    if (!item->graph_reference)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM graph match source must have a graph reference");

    SelectSourceListEntry result;
    result.graph_reference = item->graph_reference;
    result.source_kind = classifySourceIntroducingClause(item->source);
    result.match = item->source ? item->source->as<GAST::GQLMatchClause>() : nullptr;
    return result;
}

}

SelectSourceListEntries classifySelectSourceList(const OPENGQL::AST::GQLSelectSourceList & source_list)
{
    SelectSourceListEntries entries;
    entries.reserve(source_list.items.size());
    for (const auto & item_ast : source_list.items)
        entries.push_back(classifySourceListEntry(item_ast));

    return entries;
}

bool isSameGraphMatchSourceList(const SelectSourceListEntries & entries)
{
    if (entries.empty())
        return false;

    const auto & graph_reference = entries.front().graph_reference;
    for (const auto & entry : entries)
    {
        if (entry.source_kind != SourceClauseKind::Match || !entry.match)
            return false;
        if (!sameGraphReference(*graph_reference, *entry.graph_reference))
            return false;
    }

    return true;
}

bool hasDifferentGraphReferences(const SelectSourceListEntries & entries)
{
    if (entries.empty())
        return false;

    const auto & graph_reference = entries.front().graph_reference;
    for (const auto & entry : entries)
    {
        if (!sameGraphReference(*graph_reference, *entry.graph_reference))
            return true;
    }

    return false;
}

namespace
{

void planSameGraphMatchSourceList(
    QueryPlan & plan,
    const SelectSourceListEntries & entries,
    ContextPtr context,
    PlanScope & scope)
{
    std::vector<const GAST::GQLMatchClause *> matches;
    matches.reserve(entries.size());
    for (const auto & entry : entries)
        matches.push_back(entry.match);

    auto source_scope = scope.makeGraphOverrideScope(entries.front().graph_reference);

    planMatchClauseSequence(plan, matches, context, source_scope);

    scope.adoptBindingsAndKeepGraph(std::move(source_scope));
}

}

void planSelectSourceList(
    QueryPlan & plan,
    const OPENGQL::AST::GQLSelectSourceList & source_list,
    ContextPtr context,
    PlanScope & scope)
{
    if (source_list.items.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM source list must contain at least one source");

    const auto entries = classifySelectSourceList(source_list);
    if (isSameGraphMatchSourceList(entries))
    {
        planSameGraphMatchSourceList(plan, entries, context, scope);
        return;
    }

    if (hasDifferentGraphReferences(entries))
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM source lists with different graph references require source composition");

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM source lists only support graph MATCH sources");
}

}
