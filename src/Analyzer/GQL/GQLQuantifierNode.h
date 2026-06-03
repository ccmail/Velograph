#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Quantifier Node represents a quantifier for edge patterns.
 *
 * Example: *1..5
 * Example: *
 * Example: +
 */
class GQLQuantifierNode final : public IQueryTreeNode {
 public:
  GQLQuantifierNode();

  /// Get lower bound (ConstantNode or nullptr for unbounded)
  const QueryTreeNodePtr &getLowerBound() const { return children[lower_bound_child_index]; }

  /// Get lower bound
  QueryTreeNodePtr &getLowerBound() { return children[lower_bound_child_index]; }

  /// Get upper bound (ConstantNode or nullptr for unbounded)
  const QueryTreeNodePtr &getUpperBound() const { return children[upper_bound_child_index]; }

  /// Get upper bound
  QueryTreeNodePtr &getUpperBound() { return children[upper_bound_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_QUANTIFIER; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t lower_bound_child_index = 0;
  static constexpr size_t upper_bound_child_index = 1;
  static constexpr size_t children_size = upper_bound_child_index + 1;
};

}
