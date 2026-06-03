#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Simplified Path Pattern Node represents simplified path pattern syntax.
 *
 * Example: -[r]->
 */
class GQLSimplifiedPathPatternNode final : public IQueryTreeNode {
 public:
  GQLSimplifiedPathPatternNode();

  /// Get expression (GQLSimplifiedPathExprNode)
  const QueryTreeNodePtr &getExpression() const { return children[expression_child_index]; }

  /// Get expression
  QueryTreeNodePtr &getExpression() { return children[expression_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_SIMPLIFIED_PATH_PATTERN; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t expression_child_index = 0;
  static constexpr size_t children_size = expression_child_index + 1;
};

}
