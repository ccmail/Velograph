#pragma once

#include <Analyzer/IQueryTreeNode.h>

namespace DB {

/** Unresolved GQL property access such as `a.age`.
 *
 * `GQLNameResolutionPass` replaces this node with a `ColumnNode` named
 * `<variable>.<property>` after validating the property against the graph header.
 */
class GQLPropertyAccessNode final : public IQueryTreeNode {
 public:
  GQLPropertyAccessNode();
  explicit GQLPropertyAccessNode(String property_name_);

  const String &getPropertyName() const { return property_name; }
  void setPropertyName(String property_name_) { property_name = std::move(property_name_); }

  const QueryTreeNodePtr &getBase() const { return children[base_child_index]; }
  QueryTreeNodePtr &getBase() { return children[base_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PROPERTY_ACCESS; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;
  void updateTreeHashImpl(HashState &state, CompareOptions) const override;
  QueryTreeNodePtr cloneImpl() const override;
  ASTPtr toASTImpl(const ConvertToASTOptions &) const override;

 private:
  String property_name;

  static constexpr size_t base_child_index = 0;
  static constexpr size_t children_size = base_child_index + 1;
};

}  // namespace DB
