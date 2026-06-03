#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Return Node represents a RETURN clause.
 *
 * Example: RETURN n.name, n.age
 * Example: RETURN DISTINCT n.name
 */
class GQLReturnNode final : public IQueryTreeNode {
 public:
  GQLReturnNode();

  /// Returns true if this is RETURN DISTINCT
  bool isDistinct() const { return distinct; }

  /// Set distinct flag
  void setDistinct(bool distinct_value) { distinct = distinct_value; }

  /// Get return items (list of expressions with optional aliases)
  const ListNode &getItems() const { return children[items_child_index]->as<const ListNode &>(); }

  /// Get return items
  ListNode &getItems() { return children[items_child_index]->as<ListNode &>(); }

  /// Get items node
  const QueryTreeNodePtr &getItemsNode() const { return children[items_child_index]; }

  /// Get items node
  QueryTreeNodePtr &getItemsNode() { return children[items_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_RETURN; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  bool distinct = false;

  static constexpr size_t items_child_index = 0;
  static constexpr size_t children_size = items_child_index + 1;
};

}
