#include <Analyzer/GQL/GQLQueryTreeBuilder.h>

#include <Analyzer/ConstantNode.h>
#include <Analyzer/FunctionNode.h>
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
#include <Analyzer/GQL/GQLPropertyAccessNode.h>
#include <Analyzer/GQL/GQLPropertyItemNode.h>
#include <Analyzer/GQL/GQLPropertyMapNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/GQL/GQLYieldNode.h>
#include <Analyzer/Identifier.h>
#include <Analyzer/IdentifierNode.h>
#include <Analyzer/ListNode.h>
#include <Common/Exception.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeNothing.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypesNumber.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
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

const char * binaryOperatorToFunctionName(GAST::GQLExpr::BinaryOperator op)
{
    using BinaryOperator = GAST::GQLExpr::BinaryOperator;
    switch (op)
    {
        case BinaryOperator::Equals:
            return "equals";
        case BinaryOperator::NotEquals:
            return "notEquals";
        case BinaryOperator::Greater:
            return "greater";
        case BinaryOperator::GreaterOrEquals:
            return "greaterOrEquals";
        case BinaryOperator::Less:
            return "less";
        case BinaryOperator::LessOrEquals:
            return "lessOrEquals";
        case BinaryOperator::Plus:
            return "plus";
        case BinaryOperator::Minus:
            return "minus";
        case BinaryOperator::Multiply:
            return "multiply";
        case BinaryOperator::Divide:
            return "divide";
        case BinaryOperator::And:
            return "and";
        case BinaryOperator::Or:
            return "or";
        case BinaryOperator::Unknown:
            return nullptr;
    }

    return nullptr;
}

QueryTreeNodePtr makeFunctionNode(const String & function_name, QueryTreeNodes arguments)
{
    auto function = std::make_shared<FunctionNode>(function_name);
    function->getArguments().getNodes() = std::move(arguments);
    return function;
}

/** Internal implementation class for building GQL QueryTree. */
class GQLQueryTreeBuilderImpl
{
public:
    QueryTreeNodePtr build(const IAST & query)
    {
        QueryTreeNodePtr result;
        if (const auto * single = query.as<GAST::GQLSingleQuery>())
            result = buildLinearQuery(*single);
        else if (const auto * combined = query.as<GAST::GQLCombinedQuery>())
            result = buildCombinedQuery(*combined);
        else
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Unsupported GQL query root: {}", query.getID(' '));

        result->setOriginalAST(query.clone());
        return result;
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
            {
                step->setOriginalAST(clause);
                steps_list->getNodes().push_back(step);
            }
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

        /// Preserve MATCH-level WHERE for predicate normalization. KEEP, YIELD, and
        /// OPTIONAL operand blocks are unsupported and must fail closed.
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

    QueryTreeNodePtr buildLabelExpression(const GAST::GQLLabelExpression & label)
    {
        auto result = std::make_shared<GQLLabelExpressionNode>();
        using ASTKind = GAST::GQLLabelExpression::Kind;
        using TreeOperator = GQLLabelExpressionNode::Operator;

        switch (label.kind)
        {
            case ASTKind::Name:
                result->setOperator(TreeOperator::Label);
                result->setLabelName(label.text);
                return result;
            case ASTKind::Negation:
                result->setOperator(TreeOperator::Not);
                break;
            case ASTKind::Conjunction:
                result->setOperator(TreeOperator::And);
                break;
            case ASTKind::Disjunction:
                result->setOperator(TreeOperator::Or);
                break;
            case ASTKind::Wildcard:
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL wildcard label expressions are not supported");
        }

        for (const auto & child : label.children)
        {
            const auto * child_label = child ? child->as<GAST::GQLLabelExpression>() : nullptr;
            if (!child_label)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL label expression has an invalid operand");
            result->getOperands().getNodes().push_back(buildLabelExpression(*child_label));
        }

        return result;
    }

    QueryTreeNodePtr buildPropertyMap(const GAST::GQLPropertyMap & map)
    {
        auto result = std::make_shared<GQLPropertyMapNode>();
        for (const auto & item_ast : map.items)
        {
            const auto * item = item_ast ? item_ast->as<GAST::GQLPropertyItem>() : nullptr;
            if (!item || !item->value)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL property map has an invalid item");

            auto item_node = std::make_shared<GQLPropertyItemNode>();
            item_node->setPropertyName(item->key);
            item_node->getValue() = buildExpression(*item->value);
            result->getItems().getNodes().push_back(std::move(item_node));
        }
        return result;
    }

    QueryTreeNodePtr buildElementWhere(const GAST::Ptr & where_ast)
    {
        const auto * where = where_ast ? where_ast->as<GAST::GQLWhereClause>() : nullptr;
        if (!where || !where->expression)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL element WHERE must contain a predicate");
        return buildExpression(*where->expression);
    }

    QueryTreeNodePtr buildPatternLabel(const GAST::Ptr & label_ast)
    {
        const auto * label = label_ast ? label_ast->as<GAST::GQLLabelExpression>() : nullptr;
        if (!label)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL pattern label has an invalid AST shape");
        return buildLabelExpression(*label);
    }

    QueryTreeNodePtr buildPatternProperties(const GAST::Ptr & properties_ast)
    {
        const auto * properties = properties_ast ? properties_ast->as<GAST::GQLPropertyMap>() : nullptr;
        if (!properties)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL pattern property map has an invalid AST shape");
        return buildPropertyMap(*properties);
    }

    QueryTreeNodePtr buildNodePattern(const GAST::GQLNodePattern & node)
    {
        auto node_pattern = std::make_shared<GQLNodePatternNode>();
        node_pattern->setElementVariable(identifierVariable(node.variable));

        if (node.label_expression)
            node_pattern->getLabelExpression() = buildPatternLabel(node.label_expression);
        if (node.properties)
            node_pattern->getPropertyMap() = buildPatternProperties(node.properties);
        if (node.where)
            node_pattern->getWhere() = buildElementWhere(node.where);

        return node_pattern;
    }

    QueryTreeNodePtr buildEdgePattern(const GAST::GQLEdgePattern & edge)
    {
        auto edge_pattern = std::make_shared<GQLEdgePatternNode>();
        edge_pattern->setElementVariable(identifierVariable(edge.variable));
        edge_pattern->setDirection(convertEdgeDirection(edge.direction));

        if (edge.label_expression)
            edge_pattern->getLabelExpression() = buildPatternLabel(edge.label_expression);
        if (edge.properties)
            edge_pattern->getPropertyMap() = buildPatternProperties(edge.properties);
        if (edge.where)
            edge_pattern->getWhere() = buildElementWhere(edge.where);
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
        QueryTreeNodePtr result;
        if (const auto * gql_expr = expr.as<GAST::GQLExpr>())
        {
            using Kind = GAST::GQLExpr::Kind;
            switch (gql_expr->kind)
            {
                case Kind::Identifier:
                    result = std::make_shared<IdentifierNode>(Identifier(gql_expr->text));
                    break;
                case Kind::Literal:
                    if (!gql_expr->literal_value)
                        throw Exception(
                            ErrorCodes::NOT_IMPLEMENTED,
                            "GQL literal '{}' has no typed value in QueryTree builder",
                            gql_expr->text);
                    result = std::make_shared<ConstantNode>(*gql_expr->literal_value);
                    break;
                case Kind::SpecialValue:
                    result = buildSpecialValue(*gql_expr);
                    break;
                case Kind::BinaryOp:
                    result = buildBinaryOp(*gql_expr);
                    break;
                case Kind::FunctionCall:
                    result = buildFunctionCall(*gql_expr);
                    break;
                case Kind::Property:
                    if (gql_expr->children.size() != 1 || !gql_expr->children.front())
                        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL property access must have one base expression");

                    result = std::make_shared<GQLPropertyAccessNode>(gql_expr->text);
                    result->as<GQLPropertyAccessNode &>().getBase() = buildExpression(*gql_expr->children.front());
                    break;
                default:
                    break;
            }
        }
        else if (const auto * identifier = expr.as<ASTIdentifier>())
        {
            result = std::make_shared<IdentifierNode>(Identifier(identifier->name_parts));
        }
        else if (const auto * literal = expr.as<ASTLiteral>())
        {
            result = std::make_shared<ConstantNode>(literal->value);
        }
        else if (const auto * function = expr.as<ASTFunction>())
        {
            auto function_node = std::make_shared<FunctionNode>(function->name);
            if (function->parameters)
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Parameterized functions are not supported in GQL QueryTree builder");

            if (function->arguments)
            {
                for (const auto & argument : function->arguments->children)
                    function_node->getArguments().getNodes().push_back(buildExpression(*argument));
            }

            result = std::move(function_node);
        }

        if (!result)
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED, "GQL expression {} is not yet supported in QueryTree builder", expr.getID(' '));

        result->setAlias(expr.tryGetAlias());
        result->setOriginalAST(expr.clone());
        return result;
    }

    QueryTreeNodePtr buildSpecialValue(const GAST::GQLExpr & expr)
    {
        using SpecialValue = GAST::GQLExpr::SpecialValue;
        switch (expr.special_value)
        {
            case SpecialValue::True:
                return std::make_shared<ConstantNode>(Field(UInt64(1)), std::make_shared<DataTypeUInt8>());
            case SpecialValue::False:
                return std::make_shared<ConstantNode>(Field(UInt64(0)), std::make_shared<DataTypeUInt8>());
            case SpecialValue::Null:
                return std::make_shared<ConstantNode>(
                    Field(), std::make_shared<DataTypeNullable>(std::make_shared<DataTypeNothing>()));
            case SpecialValue::SessionUser:
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL SESSION_USER is not yet supported in QueryTree builder");
            case SpecialValue::Unknown:
                throw Exception(
                    ErrorCodes::NOT_IMPLEMENTED,
                    "GQL special value '{}' is not supported in QueryTree builder",
                    expr.text);
        }

        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown GQL special value kind");
    }

    QueryTreeNodePtr buildFunctionCall(const GAST::GQLExpr & expr)
    {
        if (expr.set_quantifier != GAST::GQLExpr::SetQuantifier::None)
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED,
                "GQL function '{}' with a set quantifier is not yet supported in QueryTree builder",
                expr.text);

        QueryTreeNodes arguments;
        arguments.reserve(expr.children.size());
        for (const auto & argument : expr.children)
        {
            if (!argument)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL function '{}' has a null argument", expr.text);
            arguments.push_back(buildExpression(*argument));
        }

        if (Poco::icompare(expr.text, "ELEMENT_ID") == 0)
        {
            if (arguments.size() != 1)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL ELEMENT_ID must have exactly one argument");
            return std::move(arguments.front());
        }

        return makeFunctionNode(expr.text, std::move(arguments));
    }

    QueryTreeNodePtr buildBinaryOp(const GAST::GQLExpr & expr)
    {
        const auto * function_name = binaryOperatorToFunctionName(expr.binary_operator);
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

};

} // anonymous namespace

QueryTreeNodePtr buildGQLQueryTree(const ASTPtr & query)
{
    if (!query)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL query AST is null");

    GQLQueryTreeBuilderImpl builder;
    return builder.build(*query);
}

} // namespace GQL

} // namespace DB
