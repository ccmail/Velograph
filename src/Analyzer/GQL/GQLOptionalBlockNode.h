#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Optional Block Node represents block-level OPTIONAL { ... } semantics.
 *
 * Different from GQLMatchNode::optional which is clause-level OPTIONAL MATCH.
 *
 * Example: OPTIONAL { MATCH (n) WHERE n.age > 18 RETURN n }
 */
class GQLOptionalBlockNode final : public IQueryTreeNode {
 public:
  GQLOptionalBlockNode();

  /// Get inner query (GQLLinearQueryNode)
  const QueryTreeNodePtr &getInnerQuery() const { return children[inner_query_child_index]; }

  /// Get inner query
  QueryTreeNodePtr &getInnerQuery() { return children[inner_query_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_OPTIONAL_BLOCK; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t inner_query_child_index = 0;
  static constexpr size_t children_size = inner_query_child_index + 1;
};

}
