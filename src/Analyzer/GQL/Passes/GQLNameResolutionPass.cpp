#include <Analyzer/GQL/Passes/GQLNameResolutionPass.h>

#include <Analyzer/ColumnNode.h>
#include <Analyzer/GQL/GQLCombinedQueryNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLLinearQueryNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLPropertyAccessNode.h>
#include <Analyzer/GQL/GQLPropertyItemNode.h>
#include <Analyzer/GQL/GQLPropertyMapNode.h>
#include <Analyzer/GQL/GQLPropertyResolution.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Analyzer/GQL/GQLFilterNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/IdentifierNode.h>
#include <Analyzer/ListNode.h>
#include <Common/Exception.h>
#include <DataTypes/DataTypesNumber.h>
#include <Functions/FunctionFactory.h>

namespace DB::ErrorCodes
{
extern const int UNKNOWN_IDENTIFIER;
}

namespace DB::GQL
{
namespace
{


void resolveExpression(
    QueryTreeNodePtr & node,
    const GQLMatchBindings & bindings,
    const QueryTreeNodePtr & source,
    const ContextPtr & context)
{
    if (!node)
        return;

    if (const auto * property = node->as<GQLPropertyAccessNode>())
    {
        const auto * base = property->getBase() ? property->getBase()->as<IdentifierNode>() : nullptr;
        if (!base)
            throw Exception(
                ErrorCodes::UNKNOWN_IDENTIFIER,
                "GQL property access base must be a visible MATCH variable");

        const auto variable_name = base->getIdentifier().getFullName();
        const auto * binding = findMatchBinding(bindings, variable_name);
        if (!binding)
            throw Exception(
                ErrorCodes::UNKNOWN_IDENTIFIER,
                "Unknown GQL MATCH variable '{}' in property access",
                variable_name);

        auto column = makePropertyColumn(*binding, property->getPropertyName(), source, context);
        if (property->hasAlias())
            column->setAlias(property->getAlias());
        node = std::move(column);
        return;
    }

    if (const auto * identifier = node->as<IdentifierNode>())
    {
        const auto name = identifier->getIdentifier().getFullName();
        const auto * binding = findMatchBinding(bindings, name);
        if (!binding)
            return;

        auto column = std::make_shared<ColumnNode>(
            NameAndTypePair{name, std::make_shared<DataTypeUInt64>()}, source);
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
    GQLReturnNode & ret,
    const GQLMatchBindings & bindings,
    const QueryTreeNodePtr & source,
    const ContextPtr & context)
{
    for (auto & item : ret.getItems().getNodes())
        resolveExpression(item, bindings, source, context);
}

void resolvePatternExpressions(
    GQLMatchNode & match,
    const GQLMatchBindings & bindings,
    const QueryTreeNodePtr & source,
    const ContextPtr & context)
{
    for (auto & pattern : match.getPathPatterns().getNodes())
    {
        auto * path = pattern ? pattern->as<GQLPathPatternNode>() : nullptr;
        auto * term = path && path->getExpression() ? path->getExpression()->as<GQLPathTermNode>() : nullptr;
        if (!term)
            continue;

        for (auto & element : term->getElements().getNodes())
        {
            QueryTreeNodePtr * property_map;
            QueryTreeNodePtr * where;
            if (auto * node = element->as<GQLNodePatternNode>())
            {
                property_map = &node->getPropertyMap();
                where = &node->getWhere();
            }
            else if (auto * edge = element->as<GQLEdgePatternNode>())
            {
                property_map = &edge->getPropertyMap();
                where = &edge->getWhere();
            }
            else
            {
                continue;
            }

            if (*property_map)
            {
                auto & items = (*property_map)->as<GQLPropertyMapNode &>().getItems().getNodes();
                for (auto & item : items)
                    resolveExpression(item->as<GQLPropertyItemNode &>().getValue(), bindings, source, context);
            }
            resolveExpression(*where, bindings, source, context);
        }
    }
}

void resolveQuery(QueryTreeNodePtr & node, const ContextPtr & context);

void resolveLinearQuery(GQLLinearQueryNode & linear, const ContextPtr & context)
{
    GQLMatchBindings bindings;
    QueryTreeNodePtr source;

    for (auto & step : linear.getSteps().getNodes())
    {
        if (auto * match = step->as<GQLMatchNode>())
        {
            bindings = collectMatchBindings(*match);
            source = step;
            resolvePatternExpressions(*match, bindings, source, context);
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
