#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Edge Pattern Node represents an edge pattern in a path.
 *
 * Example: -[r:KNOWS]->
 * Example: -[r:KNOWS|FOLLOWS*1..5 {since: 2020}]->
 */
class GQLEdgePatternNode final : public IQueryTreeNode {
 public:
  enum class Direction : uint8_t {
    Left,               // <-
    Right,              // ->
    Undirected,         // -
    LeftOrRight,        // <->
    LeftOrUndirected,   // <~
    UndirectedOrRight,  // ~>
    Any,                // ~
  };

  GQLEdgePatternNode();

  /// Get element variable name (e.g., 'r' in '-[r:KNOWS]->')
  const String &getElementVariable() const { return element_variable; }

  /// Set element variable name
  void setElementVariable(String element_variable_value) { element_variable = std::move(element_variable_value); }

  /// Get edge direction
  Direction getDirection() const { return direction; }

  /// Set edge direction
  void setDirection(Direction direction_value) { direction = direction_value; }

  /// Get label expression (GQLLabelExpressionNode or nullptr)
  const QueryTreeNodePtr &getLabelExpression() const { return children[label_expression_child_index]; }

  /// Get label expression
  QueryTreeNodePtr &getLabelExpression() { return children[label_expression_child_index]; }

  /// Get quantifier (GQLQuantifierNode or nullptr)
  const QueryTreeNodePtr &getQuantifier() const { return children[quantifier_child_index]; }

  /// Get quantifier
  QueryTreeNodePtr &getQuantifier() { return children[quantifier_child_index]; }

  /// Get property map (GQLPropertyMapNode or nullptr)
  const QueryTreeNodePtr &getPropertyMap() const { return children[property_map_child_index]; }

  /// Get property map
  QueryTreeNodePtr &getPropertyMap() { return children[property_map_child_index]; }

  /// Get WHERE predicate (expression or nullptr)
  const QueryTreeNodePtr &getWhere() const { return children[where_child_index]; }

  /// Get WHERE predicate
  QueryTreeNodePtr &getWhere() { return children[where_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_EDGE_PATTERN; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  String element_variable;
  Direction direction = Direction::Right;

  static constexpr size_t label_expression_child_index = 0;
  static constexpr size_t quantifier_child_index = 1;
  static constexpr size_t property_map_child_index = 2;
  static constexpr size_t where_child_index = 3;
  static constexpr size_t children_size = where_child_index + 1;
};

}
