#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Simplified Path Expr Node represents simplified path expression.
 *
 * Example: -[r:KNOWS*1..5]->
 */
class GQLSimplifiedPathExprNode final : public IQueryTreeNode {
 public:
  GQLSimplifiedPathExprNode();

  /// Get edge pattern (GQLEdgePatternNode)
  const QueryTreeNodePtr &getEdgePattern() const { return children[edge_pattern_child_index]; }

  /// Get edge pattern
  QueryTreeNodePtr &getEdgePattern() { return children[edge_pattern_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_SIMPLIFIED_PATH_EXPR; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t edge_pattern_child_index = 0;
  static constexpr size_t children_size = edge_pattern_child_index + 1;
};

}
