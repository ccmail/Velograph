#include <Analyzer/GQL/Passes/GQLNameResolutionPass.h>

#include <Analyzer/ColumnNode.h>
#include <Analyzer/GQL/GQLCombinedQueryNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLLinearQueryNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Analyzer/GQL/GQLFilterNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/IdentifierNode.h>
#include <Analyzer/ListNode.h>
#include <Core/NamesAndTypes.h>
#include <DataTypes/DataTypesNumber.h>
#include <Functions/FunctionFactory.h>

namespace DB::GQL
{
namespace
{

/// Until real graph storage defines element value types, every MATCH binding is a
/// placeholder UInt64 column, matching the header produced by Graph::MatchStep.
void addBinding(NamesAndTypes & bindings, const String & name)
{
    if (name.empty())
        return;
    for (const auto & binding : bindings)
        if (binding.name == name)
            return;
    bindings.emplace_back(name, std::make_shared<DataTypeUInt64>());
}

void collectPathBindings(const QueryTreeNodePtr & pattern, NamesAndTypes & bindings)
{
    const auto * path = pattern ? pattern->as<GQLPathPatternNode>() : nullptr;
    if (!path)
        return;

    const auto & expression = path->getExpression();
    const auto * term = expression ? expression->as<GQLPathTermNode>() : nullptr;
    if (!term)
        return;

    for (const auto & element : term->getElements().getNodes())
    {
        if (const auto * node = element->as<GQLNodePatternNode>())
            addBinding(bindings, node->getElementVariable());
        else if (const auto * edge = element->as<GQLEdgePatternNode>())
            addBinding(bindings, edge->getElementVariable());
    }
}

void collectMatchBindings(const GQLMatchNode & match, NamesAndTypes & bindings)
{
    for (const auto & pattern : match.getPathPatterns().getNodes())
        collectPathBindings(pattern, bindings);
}

const NameAndTypePair * findBinding(const NamesAndTypes & bindings, const String & name)
{
    for (const auto & binding : bindings)
        if (binding.name == name)
            return &binding;
    return nullptr;
}

void resolveExpression(
    QueryTreeNodePtr & node, const NamesAndTypes & bindings, const QueryTreeNodePtr & source, const ContextPtr & context)
{
    if (!node)
        return;

    if (const auto * identifier = node->as<IdentifierNode>())
    {
        const auto name = identifier->getIdentifier().getFullName();
        const auto * binding = findBinding(bindings, name);
        if (!binding)
            return;

        auto column = std::make_shared<ColumnNode>(*binding, source);
        if (identifier->hasAlias())
            column->setAlias(identifier->getAlias());

        node = std::move(column);
        return;
    }

    if (auto * function = node->as<FunctionNode>())
    {
        /// Resolve arguments first (bottom-up) so their types are known, then resolve the
        /// function itself via FunctionFactory. This mirrors how the SQL QueryAnalysisPass
        /// resolves a function after its arguments.
        for (auto & argument : function->getArguments().getNodes())
            resolveExpression(argument, bindings, source, context);

        if (!function->isResolved())
            function->resolveAsFunction(FunctionFactory::instance().get(function->getFunctionName(), context));
    }
}

void resolveReturnItems(
    GQLReturnNode & ret, const NamesAndTypes & bindings, const QueryTreeNodePtr & source, const ContextPtr & context)
{
    for (auto & item : ret.getItems().getNodes())
        resolveExpression(item, bindings, source, context);
}

void resolveQuery(QueryTreeNodePtr & node, const ContextPtr & context);

void resolveLinearQuery(GQLLinearQueryNode & linear, const ContextPtr & context)
{
    NamesAndTypes bindings;
    QueryTreeNodePtr source;

    for (auto & step : linear.getSteps().getNodes())
    {
        if (auto * match = step->as<GQLMatchNode>())
        {
            collectMatchBindings(*match, bindings);
            source = step;
            if (match->getWhere())
                resolveExpression(match->getWhere(), bindings, source, context);
        }
        else if (auto * ret = step->as<GQLReturnNode>())
        {
            resolveReturnItems(*ret, bindings, source, context);
        }
        else if (auto * filter = step->as<GQLFilterNode>())
        {
            resolveExpression(filter->getPredicate(), bindings, source, context);
        }
    }
}

void resolveCombinedQuery(GQLCombinedQueryNode & combined, const ContextPtr & context)
{
    for (auto & query : combined.getQueries().getNodes())
        resolveQuery(query, context);
}

void resolveQuery(QueryTreeNodePtr & node, const ContextPtr & context)
{
    if (!node)
        return;

    if (auto * linear = node->as<GQLLinearQueryNode>())
        resolveLinearQuery(*linear, context);
    else if (auto * combined = node->as<GQLCombinedQueryNode>())
        resolveCombinedQuery(*combined, context);
}

}

void GQLNameResolutionPass::run(QueryTreeNodePtr & query_tree_node, ContextPtr context)
{
    resolveQuery(query_tree_node, context);
}

}
