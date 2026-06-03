#include <Analyzer/GQL/GQLQueryTreeBuilder.h>

#include <Analyzer/GQL/GQLCombinedQueryNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLFilterNode.h>
#include <Analyzer/GQL/GQLKeepNode.h>
#include <Analyzer/GQL/GQLLabelExpressionNode.h>
#include <Analyzer/GQL/GQLLinearQueryNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLOrderByNode.h>
#include <Analyzer/GQL/GQLPageNode.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/GQL/GQLYieldNode.h>
#include <Analyzer/ConstantNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/Identifier.h>
#include <Analyzer/IdentifierNode.h>
#include <Analyzer/ListNode.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeNothing.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <IO/ReadBufferFromString.h>
#include <IO/ReadHelpers.h>
#include <Common/Exception.h>
#include <Common/StringUtils.h>
#include <Parsers/graph/GraphAST.h>

#include <Poco/String.h>

#include <utility>

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

/// Parse a GQL literal token into a constant value, mirroring the literal semantics used by
/// the existing GQL expression lowering: TRUE/FALSE/NULL, quoted strings, and numeric
/// literals (float when it contains '.', 'e' or 'E'; signed when it starts with '-';
/// unsigned otherwise).
std::pair<Field, DataTypePtr> parseGQLLiteral(const String & raw)
{
    String text = raw;
    trim(text);
    const String upper = Poco::toUpper(text);

    if (upper == "TRUE")
        return {Field(UInt64(1)), std::make_shared<DataTypeUInt8>()};
    if (upper == "FALSE")
        return {Field(UInt64(0)), std::make_shared<DataTypeUInt8>()};
    if (upper == "NULL")
        return {Field(), std::make_shared<DataTypeNullable>(std::make_shared<DataTypeNothing>())};

    if (text.size() >= 2 && ((text.front() == '\'' && text.back() == '\'') || (text.front() == '"' && text.back() == '"')))
    {
        ReadBufferFromString in(text);
        String value;
        if (text.starts_with('"'))
            readDoubleQuotedStringWithSQLStyle(value, in);
        else
            readQuotedStringWithSQLStyle(value, in);
        assertEOF(in);
        return {Field(value), std::make_shared<DataTypeString>()};
    }

    try
    {
        if (text.find_first_of(".eE") != String::npos)
            return {Field(parseFromString<Float64>(text)), std::make_shared<DataTypeFloat64>()};
        if (text.starts_with('-'))
            return {Field(parseFromString<Int64>(text)), std::make_shared<DataTypeInt64>()};
        return {Field(parseFromString<UInt64>(text)), std::make_shared<DataTypeUInt64>()};
    }
    catch (...)
    {
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL literal in QueryTree builder: {}", raw);
    }
}

String normalizedOperator(String op)
{
    trim(op);
    return Poco::toUpper(op);
}

/// Map a GQL binary operator token to a ClickHouse scalar function name, matching the
/// operator set understood by the existing GQL expression lowering.
const char * binaryOperatorToFunctionName(const String & op)
{
    if (op == "=")
        return "equals";
    if (op == "<>" || op == "!=")
        return "notEquals";
    if (op == ">")
        return "greater";
    if (op == ">=")
        return "greaterOrEquals";
    if (op == "<")
        return "less";
    if (op == "<=")
        return "lessOrEquals";
    if (op == "+")
        return "plus";
    if (op == "-")
        return "minus";
    if (op == "*")
        return "multiply";
    if (op == "/")
        return "divide";
    if (op == "AND")
        return "and";
    if (op == "OR")
        return "or";
    return nullptr;
}

QueryTreeNodePtr makeFunctionNode(const String & function_name, QueryTreeNodes arguments)
{
    auto function = std::make_shared<FunctionNode>(function_name);
    function->getArguments().getNodes() = std::move(arguments);
    return function;
}

/** Internal implementation class for building GQL QueryTree.
 *
 * This class traverses the GQL Parser AST and constructs corresponding
 * QueryTree nodes. It maintains context during the traversal.
 */
class GQLQueryTreeBuilderImpl
{
public:
    explicit GQLQueryTreeBuilderImpl(ContextPtr context_) : context(std::move(context_)) { }

    QueryTreeNodePtr build(const IAST & query)
    {
        if (const auto * single = query.as<GAST::GQLSingleQuery>())
            return buildLinearQuery(*single);

        if (const auto * combined = query.as<GAST::GQLCombinedQuery>())
            return buildCombinedQuery(*combined);

        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unsupported GQL query root: {}", query.getID(' '));
    }

private:
    QueryTreeNodePtr buildLinearQuery(const GAST::GQLSingleQuery & query)
    {
        auto linear_node = std::make_shared<GQLLinearQueryNode>();
        auto steps_list = std::make_shared<ListNode>();

        // Convert each clause to a QueryTree step
        for (const auto & clause : query.clauses)
        {
            if (auto step = buildClause(clause))
                steps_list->getNodes().push_back(step);
        }

        linear_node->getStepsNode() = std::move(steps_list);
        return linear_node;
    }

    QueryTreeNodePtr buildCombinedQuery(const GAST::GQLCombinedQuery & query)
    {
        auto combined_node = std::make_shared<GQLCombinedQueryNode>();
        auto queries_list = std::make_shared<ListNode>();

        // Convert each subquery
        for (const auto & subquery : query.queries)
        {
            if (!subquery)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query has null subquery");

            queries_list->getNodes().push_back(build(*subquery));
        }

        combined_node->getQueriesNode() = std::move(queries_list);

        // Convert operators
        std::vector<GQLCombinedQueryNode::CombinedOperator> operators;
        operators.reserve(query.operators.size());

        for (auto op : query.operators)
            operators.push_back(convertOperator(op));

        combined_node->setOperators(std::move(operators));

        return combined_node;
    }

    QueryTreeNodePtr buildClause(const ASTPtr & clause)
    {
        if (!clause)
            return nullptr;

        if (const auto * match = clause->as<GAST::GQLMatchClause>())
            return buildMatchClause(*match);

        if (const auto * where = clause->as<GAST::GQLWhereClause>())
            return buildFilterClause(*where);

        if (const auto * ret = clause->as<GAST::GQLReturnClause>())
            return buildReturnClause(*ret);

        if (const auto * order_by = clause->as<GAST::GQLOrderByClause>())
            return buildOrderByClause(*order_by);

        if (const auto * page = clause->as<GAST::GQLPageClause>())
            return buildPageClause(*page);

        // TODO: Add support for other clause types:
        // - GQLSelectClause
        // - GQLLetClause
        // - GQLForClause
        // - GQLCallClauseBase (inline/named)
        // - GQLFinishClause
        // - etc.

        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED, "GQL clause not yet supported in QueryTree builder: {}", clause->getID(' '));
    }

    QueryTreeNodePtr buildMatchClause(const GAST::GQLMatchClause & match)
    {
        auto match_node = std::make_shared<GQLMatchNode>();

        match_node->setOptional(match.optional);
        match_node->setMatchMode(match.match_mode);

        auto patterns_list = std::make_shared<ListNode>();
        for (const auto & pattern : match.path_patterns)
        {
            if (!pattern)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH path pattern is null");

            const auto * path = pattern->as<GAST::GQLPathPattern>();
            if (!path)
                throw Exception(
                    ErrorCodes::LOGICAL_ERROR, "GQL MATCH path pattern must be GQLPathPattern, got {}", pattern->getID(' '));

            patterns_list->getNodes().push_back(buildPathPattern(*path));
        }
        match_node->getPathPatternsNode() = std::move(patterns_list);

        /// MATCH-level WHERE / KEEP / YIELD and OPTIONAL operand blocks are not represented in the
        /// QueryTree builder yet; fail closed instead of silently dropping them.
        if (match.where)
        {
            const auto * where = match.where->as<GAST::GQLWhereClause>();
            if (!where || !where->expression)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH WHERE must be a GQLWhereClause with a predicate");
            match_node->getWhere() = buildExpression(*where->expression);
        }
        if (match.keep_clause)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH ... KEEP is not yet supported in QueryTree builder");
        if (!match.yield_items.empty())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH ... YIELD is not yet supported in QueryTree builder");
        if (match.optional_operand_block)
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED, "GQL OPTIONAL MATCH operand block is not yet supported in QueryTree builder");

        return match_node;
    }

    static String identifierVariable(const GAST::Ptr & variable)
    {
        if (!variable)
            return {};

        const auto * expr = variable->as<GAST::GQLExpr>();
        if (!expr || expr->kind != GAST::GQLExpr::Kind::Identifier)
            return {};

        return expr->text;
    }

    static GQLEdgePatternNode::Direction convertEdgeDirection(GAST::EdgeDirection direction)
    {
        switch (direction)
        {
            case GAST::EdgeDirection::Left:
                return GQLEdgePatternNode::Direction::Left;
            case GAST::EdgeDirection::Right:
                return GQLEdgePatternNode::Direction::Right;
            case GAST::EdgeDirection::Undirected:
                return GQLEdgePatternNode::Direction::Undirected;
            case GAST::EdgeDirection::LeftOrRight:
                return GQLEdgePatternNode::Direction::LeftOrRight;
            case GAST::EdgeDirection::LeftOrUndirected:
                return GQLEdgePatternNode::Direction::LeftOrUndirected;
            case GAST::EdgeDirection::UndirectedOrRight:
                return GQLEdgePatternNode::Direction::UndirectedOrRight;
            case GAST::EdgeDirection::Any:
                return GQLEdgePatternNode::Direction::Any;
        }

        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown GQL edge direction");
    }

    QueryTreeNodePtr buildNodePattern(const GAST::GQLNodePattern & node)
    {
        auto node_pattern = std::make_shared<GQLNodePatternNode>();
        node_pattern->setElementVariable(identifierVariable(node.variable));

        if (node.label_expression)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL node label expression is not yet supported in QueryTree builder");
        if (node.properties)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL node property map is not yet supported in QueryTree builder");
        if (node.where)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL node WHERE predicate is not yet supported in QueryTree builder");

        return node_pattern;
    }

    QueryTreeNodePtr buildEdgePattern(const GAST::GQLEdgePattern & edge)
    {
        auto edge_pattern = std::make_shared<GQLEdgePatternNode>();
        edge_pattern->setElementVariable(identifierVariable(edge.variable));
        edge_pattern->setDirection(convertEdgeDirection(edge.direction));

        if (edge.label_expression)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL edge label expression is not yet supported in QueryTree builder");
        if (edge.properties)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL edge property map is not yet supported in QueryTree builder");
        if (edge.where)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL edge WHERE predicate is not yet supported in QueryTree builder");
        if (edge.quantifier)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL edge quantifier is not yet supported in QueryTree builder");

        return edge_pattern;
    }

    QueryTreeNodePtr buildPathTerm(const GAST::GQLPathTerm & term)
    {
        if (term.factors.empty() || term.factors.size() % 2 == 0)
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED,
                "GQL path term must contain an odd number of alternating node and edge factors");

        auto term_node = std::make_shared<GQLPathTermNode>();
        auto elements = std::make_shared<ListNode>();

        for (size_t i = 0; i < term.factors.size(); ++i)
        {
            const auto & factor = term.factors[i];
            if (!factor)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL path factor is null");

            if (i % 2 == 0)
            {
                const auto * node = factor->as<GAST::GQLNodePattern>();
                if (!node)
                    throw Exception(
                        ErrorCodes::LOGICAL_ERROR, "GQL path factor must be GQLNodePattern, got {}", factor->getID(' '));
                elements->getNodes().push_back(buildNodePattern(*node));
            }
            else
            {
                const auto * edge = factor->as<GAST::GQLEdgePattern>();
                if (!edge)
                    throw Exception(
                        ErrorCodes::LOGICAL_ERROR, "GQL path factor must be GQLEdgePattern, got {}", factor->getID(' '));
                elements->getNodes().push_back(buildEdgePattern(*edge));
            }
        }

        term_node->getElementsNode() = std::move(elements);
        return term_node;
    }

    QueryTreeNodePtr buildPathExpression(const GAST::Ptr & expression)
    {
        if (!expression)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL path pattern has no expression");

        if (const auto * term = expression->as<GAST::GQLPathTerm>())
            return buildPathTerm(*term);

        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED,
            "GQL path expression {} is not yet supported in QueryTree builder",
            expression->getID(' '));
    }

    QueryTreeNodePtr buildPathPattern(const GAST::GQLPathPattern & path)
    {
        auto path_node = std::make_shared<GQLPathPatternNode>();
        path_node->setPathVariable(identifierVariable(path.variable));

        if (path.prefix)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL path prefix is not yet supported in QueryTree builder");

        path_node->getExpression() = buildPathExpression(path.expression);
        return path_node;
    }

    QueryTreeNodePtr buildExpression(const IAST & expr)
    {
        if (const auto * gql_expr = expr.as<GAST::GQLExpr>())
        {
            if (gql_expr->kind == GAST::GQLExpr::Kind::Identifier)
                return std::make_shared<IdentifierNode>(Identifier(gql_expr->text));

            if (gql_expr->kind == GAST::GQLExpr::Kind::Literal)
            {
                auto [value, type] = parseGQLLiteral(gql_expr->text);
                return std::make_shared<ConstantNode>(std::move(value), std::move(type));
            }

            if (gql_expr->kind == GAST::GQLExpr::Kind::BinaryOp)
                return buildBinaryOp(*gql_expr);
        }

        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED, "GQL expression {} is not yet supported in QueryTree builder", expr.getID(' '));
    }

    QueryTreeNodePtr buildBinaryOp(const GAST::GQLExpr & expr)
    {
        const auto * function_name = binaryOperatorToFunctionName(normalizedOperator(expr.text));
        if (!function_name)
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED, "GQL binary operator '{}' is not yet supported in QueryTree builder", expr.text);

        if (expr.children.size() != 2 || !expr.children[0] || !expr.children[1])
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL binary operator must have two operands");

        QueryTreeNodes arguments;
        arguments.push_back(buildExpression(*expr.children[0]));
        arguments.push_back(buildExpression(*expr.children[1]));
        return makeFunctionNode(function_name, std::move(arguments));
    }

    QueryTreeNodePtr buildFilterClause(const GAST::GQLWhereClause & where)
    {
        if (!where.expression)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL FILTER/WHERE clause must have a predicate");

        auto filter_node = std::make_shared<GQLFilterNode>();
        filter_node->getPredicate() = buildExpression(*where.expression);
        return filter_node;
    }

    QueryTreeNodePtr buildReturnClause(const GAST::GQLReturnClause & ret)
    {
        if (ret.return_all)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL RETURN * is not yet supported in QueryTree builder");
        if (ret.group_by)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL RETURN ... GROUP BY is not yet supported in QueryTree builder");

        auto return_node = std::make_shared<GQLReturnNode>();
        return_node->setDistinct(ret.distinct);

        auto items_list = std::make_shared<ListNode>();
        for (const auto & item_ast : ret.items)
        {
            if (!item_ast)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL RETURN item is null");

            const auto * item = item_ast->as<GAST::GQLAliasedItem>();
            if (!item)
                throw Exception(
                    ErrorCodes::LOGICAL_ERROR, "GQL RETURN item must be GQLAliasedItem, got {}", item_ast->getID(' '));
            if (!item->expression)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL RETURN item has no expression");

            auto expr_node = buildExpression(*item->expression);
            if (!item->alias.empty())
                expr_node->setAlias(item->alias);

            items_list->getNodes().push_back(std::move(expr_node));
        }
        return_node->getItemsNode() = std::move(items_list);

        return return_node;
    }

    QueryTreeNodePtr buildOrderByClause(const GAST::GQLOrderByClause & order_by)
    {
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED, "GQL ORDER BY is not yet supported in QueryTree builder: {}", order_by.getID(' '));
    }

    QueryTreeNodePtr buildPageClause(const GAST::GQLPageClause & page)
    {
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED, "GQL OFFSET/LIMIT is not yet supported in QueryTree builder: {}", page.getID(' '));
    }

    GQLCombinedQueryNode::CombinedOperator convertOperator(GAST::CombinedQueryOperator op)
    {
        switch (op)
        {
            case GAST::CombinedQueryOperator::UnionAll:
                return GQLCombinedQueryNode::CombinedOperator::UNION_ALL;
            case GAST::CombinedQueryOperator::UnionDistinct:
                return GQLCombinedQueryNode::CombinedOperator::UNION_DISTINCT;
            case GAST::CombinedQueryOperator::ExceptAll:
                return GQLCombinedQueryNode::CombinedOperator::EXCEPT_ALL;
            case GAST::CombinedQueryOperator::ExceptDistinct:
                return GQLCombinedQueryNode::CombinedOperator::EXCEPT_DISTINCT;
            case GAST::CombinedQueryOperator::IntersectAll:
                return GQLCombinedQueryNode::CombinedOperator::INTERSECT_ALL;
            case GAST::CombinedQueryOperator::IntersectDistinct:
                return GQLCombinedQueryNode::CombinedOperator::INTERSECT_DISTINCT;
            case GAST::CombinedQueryOperator::Otherwise:
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL OTHERWISE combined query operator is not supported");
        }

        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown GQL combined query operator");
    }

    ContextPtr context;
};

} // anonymous namespace

QueryTreeNodePtr buildGQLQueryTree(const IAST & query, ContextPtr context)
{
    GQLQueryTreeBuilderImpl builder(std::move(context));
    return builder.build(query);
}

} // namespace GQL

} // namespace DB
