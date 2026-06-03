#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Property Map Node represents a property map.
 *
 * Example: {age: 18, name: 'Alice'}
 */
class GQLPropertyMapNode final : public IQueryTreeNode {
 public:
  GQLPropertyMapNode();

  /// Get property items (list of GQLPropertyItemNode)
  const ListNode &getItems() const { return children[items_child_index]->as<const ListNode &>(); }

  /// Get property items
  ListNode &getItems() { return children[items_child_index]->as<ListNode &>(); }

  /// Get items node
  const QueryTreeNodePtr &getItemsNode() const { return children[items_child_index]; }

  /// Get items node
  QueryTreeNodePtr &getItemsNode() { return children[items_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PROPERTY_MAP; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t items_child_index = 0;
  static constexpr size_t children_size = items_child_index + 1;
};

}
