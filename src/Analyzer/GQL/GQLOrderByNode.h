#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Order By Node represents ORDER BY clause.
 *
 * Example: ORDER BY n.age DESC, n.name ASC
 */
class GQLOrderByNode final : public IQueryTreeNode {
 public:
  GQLOrderByNode();

  /// Get sort items (list of SortNode)
  const ListNode &getSortItems() const { return children[sort_items_child_index]->as<const ListNode &>(); }

  /// Get sort items
  ListNode &getSortItems() { return children[sort_items_child_index]->as<ListNode &>(); }

  /// Get sort items node
  const QueryTreeNodePtr &getSortItemsNode() const { return children[sort_items_child_index]; }

  /// Get sort items node
  QueryTreeNodePtr &getSortItemsNode() { return children[sort_items_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_ORDER_BY; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t sort_items_child_index = 0;
  static constexpr size_t children_size = sort_items_child_index + 1;
};

}
