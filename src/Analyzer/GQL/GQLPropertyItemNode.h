#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Property Item Node represents a single property in a property map.
 *
 * Example: age: 18
 */
class GQLPropertyItemNode final : public IQueryTreeNode {
 public:
  GQLPropertyItemNode();

  /// Get property name
  const String &getPropertyName() const { return property_name; }

  /// Set property name
  void setPropertyName(String property_name_value) { property_name = std::move(property_name_value); }

  /// Get property value expression
  const QueryTreeNodePtr &getValue() const { return children[value_child_index]; }

  /// Get property value expression
  QueryTreeNodePtr &getValue() { return children[value_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PROPERTY_ITEM; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  String property_name;

  static constexpr size_t value_child_index = 0;
  static constexpr size_t children_size = value_child_index + 1;
};

}
