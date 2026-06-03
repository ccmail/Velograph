#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Filter Node represents a FILTER clause.
 *
 * Example: FILTER n.age > 18
 */
class GQLFilterNode final : public IQueryTreeNode {
 public:
  GQLFilterNode();

  /// Get filter predicate expression
  const QueryTreeNodePtr &getPredicate() const { return children[predicate_child_index]; }

  /// Get filter predicate expression
  QueryTreeNodePtr &getPredicate() { return children[predicate_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_FILTER; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t predicate_child_index = 0;
  static constexpr size_t children_size = predicate_child_index + 1;
};

}
