#include <Interpreters/GQL/MatchSpecBuilder.h>

#include <Parsers/IAST.h>
#include <Parsers/graph/AST/GQLExpr.h>
#include <Parsers/graph/AST/GQLKeepClause.h>
#include <Parsers/graph/AST/GQLLabelExpression.h>
#include <Parsers/graph/AST/GQLMatchStatementBlock.h>
#include <Parsers/graph/AST/GQLPropertyMap.h>
#include <Parsers/graph/AST/GQLQuantifier.h>
#include <Parsers/graph/AST/GQLWhereClause.h>

namespace DB::GQL
{
namespace
{

ASTPtr cloneOrNull(const IAST * ast)
{
    return ast ? ast->clone() : nullptr;
}

Graph::MatchEdgeDirection makeEdgeDirection(EdgeBinding::Direction direction)
{
    switch (direction)
    {
        case EdgeBinding::Direction::Outgoing:
            return Graph::MatchEdgeDirection::Outgoing;
        case EdgeBinding::Direction::Incoming:
            return Graph::MatchEdgeDirection::Incoming;
        case EdgeBinding::Direction::Undirected:
            return Graph::MatchEdgeDirection::Undirected;
        case EdgeBinding::Direction::IncomingOrOutgoing:
            return Graph::MatchEdgeDirection::IncomingOrOutgoing;
        case EdgeBinding::Direction::IncomingOrUndirected:
            return Graph::MatchEdgeDirection::IncomingOrUndirected;
        case EdgeBinding::Direction::UndirectedOrOutgoing:
            return Graph::MatchEdgeDirection::UndirectedOrOutgoing;
        case EdgeBinding::Direction::Any:
            return Graph::MatchEdgeDirection::Any;
    }

    return Graph::MatchEdgeDirection::Any;
}

Graph::MatchMode makeMatchMode(OPENGQL::AST::GraphMatchMode match_mode)
{
    switch (match_mode)
    {
        case OPENGQL::AST::GraphMatchMode::None:
            return Graph::MatchMode::None;
        case OPENGQL::AST::GraphMatchMode::RepeatableElements:
            return Graph::MatchMode::RepeatableElements;
        case OPENGQL::AST::GraphMatchMode::RepeatableElementBindings:
            return Graph::MatchMode::RepeatableElementBindings;
        case OPENGQL::AST::GraphMatchMode::DifferentEdges:
            return Graph::MatchMode::DifferentEdges;
        case OPENGQL::AST::GraphMatchMode::DifferentEdgeBindings:
            return Graph::MatchMode::DifferentEdgeBindings;
    }

    return Graph::MatchMode::None;
}

Graph::MatchPathAlternationKind makeAlternationKind(PathBinding::AlternationKind kind)
{
    switch (kind)
    {
        case PathBinding::AlternationKind::None:
            return Graph::MatchPathAlternationKind::None;
        case PathBinding::AlternationKind::Union:
            return Graph::MatchPathAlternationKind::Union;
        case PathBinding::AlternationKind::MultisetAlternation:
            return Graph::MatchPathAlternationKind::MultisetAlternation;
    }

    return Graph::MatchPathAlternationKind::None;
}

Graph::MatchNodeSpec makeNodeSpec(const NodeBinding & node)
{
    return Graph::MatchNodeSpec{
        .variable = node.variable,
        .label_expression = cloneOrNull(node.label),
        .properties = cloneOrNull(node.properties),
        .predicate = cloneOrNull(node.where),
    };
}

Graph::MatchEdgeSpec makeEdgeSpec(const EdgeBinding & edge)
{
    return Graph::MatchEdgeSpec{
        .variable = edge.variable,
        .direction = makeEdgeDirection(edge.direction),
        .label_expression = cloneOrNull(edge.label),
        .properties = cloneOrNull(edge.properties),
        .predicate = cloneOrNull(edge.where),
        .quantifier = cloneOrNull(edge.quantifier),
    };
}

Graph::MatchPathSpec makePathSpec(const PathBinding & path)
{
    Graph::MatchPathSpec result;
    result.variable = path.variable;
    result.prefix = cloneOrNull(path.prefix);
    result.alternation_kind = makeAlternationKind(path.alternation_kind);
    result.alternatives.reserve(path.alternatives.size());
    for (const auto & alternative : path.alternatives)
        result.alternatives.push_back(makePathSpec(alternative));

    result.nodes.reserve(path.nodes.size());
    for (const auto & node : path.nodes)
        result.nodes.push_back(makeNodeSpec(node));

    result.edges.reserve(path.edges.size());
    for (const auto & edge : path.edges)
        result.edges.push_back(makeEdgeSpec(edge));

    return result;
}

String yieldVariable(const OPENGQL::AST::GQLExpr & item)
{
    if (item.kind != OPENGQL::AST::GQLExpr::Kind::Identifier)
        return {};

    return item.text;
}

Graph::MatchClauseSpec makeMatchClauseSpec(const MatchPlan & match)
{
    Graph::MatchClauseSpec result;
    result.optional = match.optional;
    result.has_match_mode = match.match_mode != OPENGQL::AST::GraphMatchMode::None;
    result.match_mode = makeMatchMode(match.match_mode);
    result.has_keep_clause = match.has_keep_clause;
    result.has_optional_operand_block = match.has_optional_operand_block;
    result.has_yield_items = !match.yield_items.empty();
    result.keep_clause = cloneOrNull(match.keep_clause);
    result.optional_operand_block = cloneOrNull(match.optional_operand_block);
    result.where_clause = cloneOrNull(match.where);

    result.yield_items.reserve(match.yield_items.size());
    result.yield_variables.reserve(match.yield_items.size());
    for (const auto * item : match.yield_items)
    {
        if (!item)
            continue;

        result.yield_items.push_back(item->clone());
        const auto variable = yieldVariable(*item);
        if (!variable.empty())
            result.yield_variables.push_back(variable);
    }

    result.paths.reserve(match.paths.size());
    for (const auto & path : match.paths)
        result.paths.push_back(makePathSpec(path));

    return result;
}

void mergeMatchClauseSpec(Graph::MatchSpec & result, const Graph::MatchClauseSpec & clause)
{
    result.optional = result.optional || clause.optional;
    result.has_match_mode = result.has_match_mode || clause.has_match_mode;
    if (result.match_mode == Graph::MatchMode::None)
        result.match_mode = clause.match_mode;
    result.has_keep_clause = result.has_keep_clause || clause.has_keep_clause;
    result.has_optional_operand_block = result.has_optional_operand_block || clause.has_optional_operand_block;
    result.has_yield_items = result.has_yield_items || clause.has_yield_items;
    if (!result.keep_clause && clause.keep_clause)
        result.keep_clause = clause.keep_clause->clone();
    if (!result.optional_operand_block && clause.optional_operand_block)
        result.optional_operand_block = clause.optional_operand_block->clone();
    if (!result.where_clause && clause.where_clause)
        result.where_clause = clause.where_clause->clone();

    for (const auto & yield_item : clause.yield_items)
    {
        if (yield_item)
            result.yield_items.push_back(yield_item->clone());
    }
    result.yield_variables.insert(result.yield_variables.end(), clause.yield_variables.begin(), clause.yield_variables.end());
    result.paths.insert(result.paths.end(), clause.paths.begin(), clause.paths.end());
}

}

Graph::MatchSpec buildMatchSpec(const MatchPlan & match)
{
    return buildMatchSpec(std::vector<MatchPlan>{match});
}

Graph::MatchSpec buildMatchSpec(const std::vector<MatchPlan> & matches)
{
    Graph::MatchSpec result;
    result.clauses.reserve(matches.size());
    for (const auto & match : matches)
    {
        auto clause = makeMatchClauseSpec(match);
        mergeMatchClauseSpec(result, clause);
        result.clauses.push_back(std::move(clause));
    }

    return result;
}

}
