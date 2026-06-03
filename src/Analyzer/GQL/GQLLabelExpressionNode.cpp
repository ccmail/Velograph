#include <Analyzer/GQL/GQLLabelExpressionNode.h>

#include <Common/assert_cast.h>
#include <Common/SipHash.h>
#include <IO/WriteBuffer.h>
#include <IO/Operators.h>

namespace DB
{

namespace ErrorCodes
{
extern const int UNSUPPORTED_METHOD;
}

GQLLabelExpressionNode::GQLLabelExpressionNode() : IQueryTreeNode(children_size) {
  children[operands_child_index] = std::make_shared<ListNode>();
}

void GQLLabelExpressionNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_LABEL_EXPRESSION id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  buffer << ", operator: ";
  switch (op) {
    case Operator::Label:
      buffer << "Label";
      break;
    case Operator::Or:
      buffer << "Or";
      break;
    case Operator::And:
      buffer << "And";
      break;
    case Operator::Not:
      buffer << "Not";
      break;
  }

  if (!label_name.empty()) buffer << ", label_name: " << label_name;

  if (getOperands().getNodes().size() > 0) {
    buffer << '\n' << std::string(indent + 2, ' ') << "OPERANDS\n";
    getOperandsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLLabelExpressionNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLLabelExpressionNode &>(rhs);
  return op == rhs_typed.op && label_name == rhs_typed.label_name;
}

void GQLLabelExpressionNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(static_cast<size_t>(op));
  state.update(label_name.size());
  state.update(label_name);
}

QueryTreeNodePtr GQLLabelExpressionNode::cloneImpl() const {
  auto result = std::make_shared<GQLLabelExpressionNode>();
  result->op = op;
  result->label_name = label_name;
  return result;
}

ASTPtr GQLLabelExpressionNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLLabelExpressionNode::toASTImpl is not implemented yet");
}

}
