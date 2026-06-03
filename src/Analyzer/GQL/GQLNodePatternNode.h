#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Node Pattern Node represents a node pattern in a path.
 *
 * Example: (n:Person {age: 18})
 * Example: (n:Person|Company WHERE n.age > 18)
 */
class GQLNodePatternNode final : public IQueryTreeNode {
 public:
  GQLNodePatternNode();

  /// Get element variable name (e.g., 'n' in '(n:Person)')
  const String &getElementVariable() const { return element_variable; }

  /// Set element variable name
  void setElementVariable(String element_variable_value) { element_variable = std::move(element_variable_value); }

  /// Get label expression (GQLLabelExpressionNode or nullptr)
  const QueryTreeNodePtr &getLabelExpression() const { return children[label_expression_child_index]; }

  /// Get label expression
  QueryTreeNodePtr &getLabelExpression() { return children[label_expression_child_index]; }

  /// Get property map (GQLPropertyMapNode or nullptr)
  const QueryTreeNodePtr &getPropertyMap() const { return children[property_map_child_index]; }

  /// Get property map
  QueryTreeNodePtr &getPropertyMap() { return children[property_map_child_index]; }

  /// Get WHERE predicate (expression or nullptr)
  const QueryTreeNodePtr &getWhere() const { return children[where_child_index]; }

  /// Get WHERE predicate
  QueryTreeNodePtr &getWhere() { return children[where_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_NODE_PATTERN; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  String element_variable;

  static constexpr size_t label_expression_child_index = 0;
  static constexpr size_t property_map_child_index = 1;
  static constexpr size_t where_child_index = 2;
  static constexpr size_t children_size = where_child_index + 1;
};

}
