#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Path Term Node represents a classic node/edge chain pattern.
 *
 * Example: (a)-[r]->(b)-[s]->(c)
 */
class GQLPathTermNode final : public IQueryTreeNode {
 public:
  GQLPathTermNode();

  /// Get elements (alternating GQLNodePatternNode and GQLEdgePatternNode)
  const ListNode &getElements() const { return children[elements_child_index]->as<const ListNode &>(); }

  /// Get elements
  ListNode &getElements() { return children[elements_child_index]->as<ListNode &>(); }

  /// Get elements node
  const QueryTreeNodePtr &getElementsNode() const { return children[elements_child_index]; }

  /// Get elements node
  QueryTreeNodePtr &getElementsNode() { return children[elements_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PATH_TERM; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t elements_child_index = 0;
  static constexpr size_t children_size = elements_child_index + 1;
};

}
