#include <Interpreters/GQL/PatternBinder.h>

#include <Common/Exception.h>
#include <Parsers/graph/AST/GQLEdgePattern.h>
#include <Parsers/graph/AST/GQLExpr.h>
#include <Parsers/graph/AST/GQLKeepClause.h>
#include <Parsers/graph/AST/GQLLabelExpression.h>
#include <Parsers/graph/AST/GQLMatchClause.h>
#include <Parsers/graph/AST/GQLMatchStatementBlock.h>
#include <Parsers/graph/AST/GQLNodePattern.h>
#include <Parsers/graph/AST/GQLPathModePrefix.h>
#include <Parsers/graph/AST/GQLPathPattern.h>
#include <Parsers/graph/AST/GQLPathPatternAlternation.h>
#include <Parsers/graph/AST/GQLPathSearchPrefix.h>
#include <Parsers/graph/AST/GQLPathTerm.h>
#include <Parsers/graph/AST/GQLPropertyMap.h>
#include <Parsers/graph/AST/GQLQuantifier.h>
#include <Parsers/graph/AST/GQLWhereClause.h>

namespace DB::ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{
namespace
{

using GQLEdgePattern = OPENGQL::AST::GQLEdgePattern;
using GQLExpr = OPENGQL::AST::GQLExpr;
using GQLKeepClause = OPENGQL::AST::GQLKeepClause;
using GQLLabelExpression = OPENGQL::AST::GQLLabelExpression;
using GQLMatchClause = OPENGQL::AST::GQLMatchClause;
using GQLMatchStatementBlock = OPENGQL::AST::GQLMatchStatementBlock;
using GQLNodePattern = OPENGQL::AST::GQLNodePattern;
using GQLPathModePrefix = OPENGQL::AST::GQLPathModePrefix;
using GQLPathPattern = OPENGQL::AST::GQLPathPattern;
using GQLPathPatternAlternation = OPENGQL::AST::GQLPathPatternAlternation;
using GQLPathSearchPrefix = OPENGQL::AST::GQLPathSearchPrefix;
using GQLPathTerm = OPENGQL::AST::GQLPathTerm;
using GQLPropertyMap = OPENGQL::AST::GQLPropertyMap;
using GQLQuantifier = OPENGQL::AST::GQLQuantifier;
using GQLWhereClause = OPENGQL::AST::GQLWhereClause;

[[noreturn]] void throwUnsupportedPathPattern(const String & reason)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL path pattern: {}", reason);
}

String astID(const IAST & ast)
{
    return ast.getID(' ');
}

String edgeDirectionToString(OPENGQL::AST::EdgeDirection direction)
{
    switch (direction)
    {
        case OPENGQL::AST::EdgeDirection::Left:
            return "Left";
        case OPENGQL::AST::EdgeDirection::Right:
            return "Right";
        case OPENGQL::AST::EdgeDirection::Undirected:
            return "Undirected";
        case OPENGQL::AST::EdgeDirection::LeftOrRight:
            return "LeftOrRight";
        case OPENGQL::AST::EdgeDirection::LeftOrUndirected:
            return "LeftOrUndirected";
        case OPENGQL::AST::EdgeDirection::UndirectedOrRight:
            return "UndirectedOrRight";
        case OPENGQL::AST::EdgeDirection::Any:
            return "Any";
    }

    return "Unknown";
}

String identifierVariable(const OPENGQL::AST::Ptr & variable)
{
    if (!variable)
        return {};

    const auto * expr = variable->as<GQLExpr>();
    if (!expr || expr->kind != GQLExpr::Kind::Identifier)
        return {};

    return expr->text;
}

template <typename T>
const T * getOptionalChild(const OPENGQL::AST::Ptr & child, const char * field_name)
{
    if (!child)
        return nullptr;

    const auto * result = child->as<T>();
    if (!result)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unexpected GQL pattern {} child type: {}", field_name, astID(*child));

    return result;
}

EdgeBinding::Direction bindEdgeDirection(OPENGQL::AST::EdgeDirection direction)
{
    // note: AST `Right` is outgoing from the preceding node, `Left` is incoming, `Undirected` stays undirected, and `Any` stays unconstrained.
    switch (direction)
    {
        case OPENGQL::AST::EdgeDirection::Right:
            return EdgeBinding::Direction::Outgoing;
        case OPENGQL::AST::EdgeDirection::Left:
            return EdgeBinding::Direction::Incoming;
        case OPENGQL::AST::EdgeDirection::Undirected:
            return EdgeBinding::Direction::Undirected;
        case OPENGQL::AST::EdgeDirection::LeftOrRight:
            return EdgeBinding::Direction::IncomingOrOutgoing;
        case OPENGQL::AST::EdgeDirection::LeftOrUndirected:
            return EdgeBinding::Direction::IncomingOrUndirected;
        case OPENGQL::AST::EdgeDirection::UndirectedOrRight:
            return EdgeBinding::Direction::UndirectedOrOutgoing;
        case OPENGQL::AST::EdgeDirection::Any:
            return EdgeBinding::Direction::Any;
    }

    throwUnsupportedPathPattern(fmt::format("edge direction {} is not supported", edgeDirectionToString(direction)));
}

NodeBinding bindNodePattern(const GQLNodePattern & node)
{
    NodeBinding binding;
    binding.variable = identifierVariable(node.variable);
    binding.label = getOptionalChild<GQLLabelExpression>(node.label_expression, "node label");
    binding.properties = getOptionalChild<GQLPropertyMap>(node.properties, "node properties");
    binding.where = getOptionalChild<GQLExpr>(node.where, "node where");
    return binding;
}

EdgeBinding bindEdgePattern(const GQLEdgePattern & edge)
{
    EdgeBinding binding;
    binding.variable = identifierVariable(edge.variable);
    binding.direction = bindEdgeDirection(edge.direction);
    binding.label = getOptionalChild<GQLLabelExpression>(edge.label_expression, "edge label");
    binding.properties = getOptionalChild<GQLPropertyMap>(edge.properties, "edge properties");
    binding.where = getOptionalChild<GQLExpr>(edge.where, "edge where");
    binding.quantifier = getOptionalChild<GQLQuantifier>(edge.quantifier, "edge quantifier");
    return binding;
}

const IAST * getPathPrefix(const OPENGQL::AST::Ptr & prefix)
{
    if (!prefix)
        return nullptr;

    if (prefix->as<GQLPathModePrefix>() || prefix->as<GQLPathSearchPrefix>())
        return prefix.get();

    throw Exception(ErrorCodes::LOGICAL_ERROR, "Unexpected GQL path prefix child type: {}", astID(*prefix));
}

PathBinding bindPathTerm(const GQLPathTerm & term)
{
    if (term.factors.empty())
        throwUnsupportedPathPattern("empty path term");

    if (term.factors.size() % 2 == 0)
        throwUnsupportedPathPattern("path term must contain an odd number of alternating node and edge factors");

    PathBinding result;
    for (size_t i = 0; i < term.factors.size(); ++i)
    {
        const auto & factor = term.factors[i];
        if (!factor)
            throwUnsupportedPathPattern(fmt::format("path factor {} is null", i));

        if (i % 2 == 0)
        {
            const auto * node = factor->as<GQLNodePattern>();
            if (!node)
                throwUnsupportedPathPattern(fmt::format("path factor {} must be a GQLNodePattern, got {}", i, astID(*factor)));

            result.nodes.push_back(bindNodePattern(*node));
        }
        else
        {
            const auto * edge = factor->as<GQLEdgePattern>();
            if (!edge)
                throwUnsupportedPathPattern(fmt::format("path factor {} must be a GQLEdgePattern, got {}", i, astID(*factor)));

            result.edges.push_back(bindEdgePattern(*edge));
        }
    }

    if (result.edges.size() + 1 != result.nodes.size())
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "Invalid bound GQL path binding: {} nodes and {} edges",
            result.nodes.size(),
            result.edges.size());

    return result;
}

PathBinding::AlternationKind bindAlternationKind(GQLPathPatternAlternation::Kind kind)
{
    switch (kind)
    {
        case GQLPathPatternAlternation::Kind::Union:
            return PathBinding::AlternationKind::Union;
        case GQLPathPatternAlternation::Kind::MultisetAlternation:
            return PathBinding::AlternationKind::MultisetAlternation;
    }

    return PathBinding::AlternationKind::None;
}

PathBinding bindPathExpression(const OPENGQL::AST::Ptr & expression)
{
    if (!expression)
        throwUnsupportedPathPattern("missing path expression");

    if (const auto * term = expression->as<GQLPathTerm>())
        return bindPathTerm(*term);

    if (const auto * alternation = expression->as<GQLPathPatternAlternation>())
    {
        PathBinding result;
        result.alternation_kind = bindAlternationKind(alternation->kind);
        result.alternatives.reserve(alternation->operands.size());
        for (const auto & operand : alternation->operands)
        {
            const auto * term = operand ? operand->as<GQLPathTerm>() : nullptr;
            if (!term)
                throwUnsupportedPathPattern("path alternation operand must be a GQLPathTerm");

            result.alternatives.push_back(bindPathTerm(*term));
        }

        return result;
    }

    throwUnsupportedPathPattern(fmt::format("path expression {} is not supported", astID(*expression)));
}

}

PathBinding bindPathPattern(const OPENGQL::AST::GQLPathPattern & path)
{
    PathBinding result = bindPathExpression(path.expression);
    result.variable = identifierVariable(path.variable);
    result.prefix = getPathPrefix(path.prefix);
    return result;
}

MatchPlan bindMatchClause(const OPENGQL::AST::GQLMatchClause & match)
{
    MatchPlan result;
    result.optional = match.optional;
    result.match_mode = match.match_mode;
    result.keep_clause = getOptionalChild<GQLKeepClause>(match.keep_clause, "match keep");
    result.optional_operand_block = getOptionalChild<GQLMatchStatementBlock>(match.optional_operand_block, "optional match operand");
    result.has_keep_clause = match.keep_clause != nullptr;
    result.has_optional_operand_block = match.optional_operand_block != nullptr;

    result.paths.reserve(match.path_patterns.size());
    for (const auto & path_ast : match.path_patterns)
    {
        if (!path_ast)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH path pattern is null");

        const auto * path = path_ast->as<GQLPathPattern>();
        if (!path)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH path pattern must be GQLPathPattern, got {}", astID(*path_ast));

        result.paths.push_back(bindPathPattern(*path));
    }

    result.yield_items.reserve(match.yield_items.size());
    for (const auto & item_ast : match.yield_items)
    {
        if (!item_ast)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH yield item is null");

        const auto * item = item_ast->as<GQLExpr>();
        if (!item)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH yield item must be GQLExpr, got {}", astID(*item_ast));

        result.yield_items.push_back(item);
    }

    if (match.where)
    {
        result.where = match.where->as<GQLWhereClause>();
        if (!result.where)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH where node must be GQLWhereClause, got {}", astID(*match.where));
    }

    return result;
}

}
