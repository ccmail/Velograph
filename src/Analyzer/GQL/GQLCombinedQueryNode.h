#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Combined Query Node represents UNION/INTERSECT/EXCEPT of GQL queries.
 *
 * Corresponds to parser AST GQLCombinedQuery.
 *
 * Example: (MATCH (n) RETURN n) UNION ALL (MATCH (m) RETURN m)
 */
class GQLCombinedQueryNode final : public IQueryTreeNode
{
public:
    enum class CombinedOperator : uint8_t
    {
        UNION_ALL,
        UNION_DISTINCT,
        EXCEPT_ALL,
        EXCEPT_DISTINCT,
        INTERSECT_ALL,
        INTERSECT_DISTINCT,
    };

    GQLCombinedQueryNode();

    /// Get queries
    const ListNode & getQueries() const
    {
        return children[queries_child_index]->as<const ListNode &>();
    }

    /// Get queries
    ListNode & getQueries()
    {
        return children[queries_child_index]->as<ListNode &>();
    }

    /// Get queries node
    const QueryTreeNodePtr & getQueriesNode() const
    {
        return children[queries_child_index];
    }

    /// Get queries node
    QueryTreeNodePtr & getQueriesNode()
    {
        return children[queries_child_index];
    }

    /// Get operators
    const std::vector<CombinedOperator> & getOperators() const
    {
        return operators;
    }

    /// Set operators
    void setOperators(std::vector<CombinedOperator> operators_value)
    {
        operators = std::move(operators_value);
    }

    QueryTreeNodeType getNodeType() const override
    {
        return QueryTreeNodeType::GQL_COMBINED_QUERY;
    }

    void dumpTreeImpl(WriteBuffer & buffer, FormatState & format_state, size_t indent) const override;

protected:
    bool isEqualImpl(const IQueryTreeNode & rhs, CompareOptions) const override;

    void updateTreeHashImpl(HashState & hash_state, CompareOptions) const override;

    QueryTreeNodePtr cloneImpl() const override;

    ASTPtr toASTImpl(const ConvertToASTOptions & options) const override;

private:
    std::vector<CombinedOperator> operators;

    static constexpr size_t queries_child_index = 0;
    static constexpr size_t children_size = queries_child_index + 1;
};

}
