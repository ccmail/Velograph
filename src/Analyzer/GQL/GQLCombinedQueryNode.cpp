#include <Analyzer/GQL/GQLCombinedQueryNode.h>

#include <Analyzer/ListNode.h>
#include <Common/assert_cast.h>
#include <Common/SipHash.h>
#include <IO/WriteBuffer.h>
#include <IO/Operators.h>
#include <Parsers/graph/GraphAST.h>

namespace DB
{

namespace ErrorCodes
{
extern const int UNSUPPORTED_METHOD;
extern const int LOGICAL_ERROR;
}

GQLCombinedQueryNode::GQLCombinedQueryNode()
    : IQueryTreeNode(children_size)
{
    children[queries_child_index] = std::make_shared<ListNode>();
}

void GQLCombinedQueryNode::dumpTreeImpl(WriteBuffer & buffer, FormatState & format_state, size_t indent) const
{
    buffer << std::string(indent, ' ') << "GQL_COMBINED_QUERY id: " << format_state.getNodeId(this);

    if (hasAlias())
        buffer << ", alias: " << getAlias();

    buffer << ", operators: [";
    for (size_t i = 0; i < operators.size(); ++i)
    {
        if (i > 0)
            buffer << ", ";
        switch (operators[i])
        {
            case CombinedOperator::UNION_ALL:
                buffer << "UNION_ALL";
                break;
            case CombinedOperator::UNION_DISTINCT:
                buffer << "UNION_DISTINCT";
                break;
            case CombinedOperator::EXCEPT_ALL:
                buffer << "EXCEPT_ALL";
                break;
            case CombinedOperator::EXCEPT_DISTINCT:
                buffer << "EXCEPT_DISTINCT";
                break;
            case CombinedOperator::INTERSECT_ALL:
                buffer << "INTERSECT_ALL";
                break;
            case CombinedOperator::INTERSECT_DISTINCT:
                buffer << "INTERSECT_DISTINCT";
                break;
        }
    }
    buffer << "]";

    buffer << '\n' << std::string(indent + 2, ' ') << "QUERIES\n";
    getQueriesNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLCombinedQueryNode::isEqualImpl(const IQueryTreeNode & rhs, CompareOptions) const
{
    const auto & rhs_typed = assert_cast<const GQLCombinedQueryNode &>(rhs);
    return operators == rhs_typed.operators;
}

void GQLCombinedQueryNode::updateTreeHashImpl(HashState & state, CompareOptions) const
{
    state.update(operators.size());
    for (auto op : operators)
        state.update(static_cast<size_t>(op));
}

QueryTreeNodePtr GQLCombinedQueryNode::cloneImpl() const
{
    auto result = std::make_shared<GQLCombinedQueryNode>();
    result->operators = operators;
    return result;
}

ASTPtr GQLCombinedQueryNode::toASTImpl(const ConvertToASTOptions & options) const
{
    namespace GAST = DB::OPENGQL::AST;

    auto combined_query = make_intrusive<GAST::GQLCombinedQuery>();

    // Convert subqueries
    const auto & queries = getQueries().getNodes();
    combined_query->queries.reserve(queries.size());

    for (const auto & query : queries)
    {
        if (!query)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query has null subquery in QueryTree");

        auto query_ast = query->toAST(options);
        combined_query->queries.push_back(query_ast);
        combined_query->children.push_back(query_ast);
    }

    // Convert operators
    combined_query->operators.reserve(operators.size());
    for (auto op : operators)
    {
        switch (op)
        {
            case CombinedOperator::UNION_ALL:
                combined_query->operators.push_back(GAST::CombinedQueryOperator::UnionAll);
                break;
            case CombinedOperator::UNION_DISTINCT:
                combined_query->operators.push_back(GAST::CombinedQueryOperator::UnionDistinct);
                break;
            case CombinedOperator::EXCEPT_ALL:
                combined_query->operators.push_back(GAST::CombinedQueryOperator::ExceptAll);
                break;
            case CombinedOperator::EXCEPT_DISTINCT:
                combined_query->operators.push_back(GAST::CombinedQueryOperator::ExceptDistinct);
                break;
            case CombinedOperator::INTERSECT_ALL:
                combined_query->operators.push_back(GAST::CombinedQueryOperator::IntersectAll);
                break;
            case CombinedOperator::INTERSECT_DISTINCT:
                combined_query->operators.push_back(GAST::CombinedQueryOperator::IntersectDistinct);
                break;
        }
    }

    return combined_query;
}

}
