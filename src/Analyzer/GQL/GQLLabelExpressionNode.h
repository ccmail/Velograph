#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Label Expression Node represents a label expression.
 *
 * Example: Person
 * Example: Person|Company
 * Example: Person&Employee
 * Example: !Person
 */
class GQLLabelExpressionNode final : public IQueryTreeNode {
 public:
  enum class Operator : uint8_t {
    Label,  // Single label
    Or,     // |
    And,    // &
    Not,    // !
  };

  GQLLabelExpressionNode();

  /// Get operator
  Operator getOperator() const { return op; }

  /// Set operator
  void setOperator(Operator op_value) { op = op_value; }

  /// Get label name (for Label operator)
  const String &getLabelName() const { return label_name; }

  /// Set label name
  void setLabelName(String label_name_value) { label_name = std::move(label_name_value); }

  /// Get operands (for Or, And, Not operators)
  const ListNode &getOperands() const { return children[operands_child_index]->as<const ListNode &>(); }

  /// Get operands
  ListNode &getOperands() { return children[operands_child_index]->as<ListNode &>(); }

  /// Get operands node
  const QueryTreeNodePtr &getOperandsNode() const { return children[operands_child_index]; }

  /// Get operands node
  QueryTreeNodePtr &getOperandsNode() { return children[operands_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_LABEL_EXPRESSION; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  Operator op = Operator::Label;
  String label_name;

  static constexpr size_t operands_child_index = 0;
  static constexpr size_t children_size = operands_child_index + 1;
};

}
