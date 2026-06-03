#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Linear Query Node represents a single GQL query with ordered clause pipeline.
 *
 * Corresponds to parser AST GQLSingleQuery but stores semantic steps rather than raw grammar nodes.
 *
 * Example: MATCH (n) WHERE n.age > 18 RETURN n ORDER BY n.age LIMIT 10
 */
class GQLLinearQueryNode final : public IQueryTreeNode
{
public:
    GQLLinearQueryNode();

    /// Get ordered clause steps
    const ListNode & getSteps() const
    {
        return children[steps_child_index]->as<const ListNode &>();
    }

    /// Get ordered clause steps
    ListNode & getSteps()
    {
        return children[steps_child_index]->as<ListNode &>();
    }

    /// Get steps node
    const QueryTreeNodePtr & getStepsNode() const
    {
        return children[steps_child_index];
    }

    /// Get steps node
    QueryTreeNodePtr & getStepsNode()
    {
        return children[steps_child_index];
    }

    QueryTreeNodeType getNodeType() const override
    {
        return QueryTreeNodeType::GQL_LINEAR_QUERY;
    }

    void dumpTreeImpl(WriteBuffer & buffer, FormatState & format_state, size_t indent) const override;

protected:
    bool isEqualImpl(const IQueryTreeNode & rhs, CompareOptions) const override;

    void updateTreeHashImpl(HashState & hash_state, CompareOptions) const override;

    QueryTreeNodePtr cloneImpl() const override;

    ASTPtr toASTImpl(const ConvertToASTOptions & options) const override;

private:
    static constexpr size_t steps_child_index = 0;
    static constexpr size_t children_size = steps_child_index + 1;
};

}
