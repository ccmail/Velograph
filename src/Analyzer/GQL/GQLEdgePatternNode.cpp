#include <Analyzer/GQL/GQLEdgePatternNode.h>

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

GQLEdgePatternNode::GQLEdgePatternNode() : IQueryTreeNode(children_size) {}

void GQLEdgePatternNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_EDGE_PATTERN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (!element_variable.empty()) buffer << ", element_variable: " << element_variable;

  buffer << ", direction: ";
  switch (direction) {
    case Direction::Left:
      buffer << "Left";
      break;
    case Direction::Right:
      buffer << "Right";
      break;
    case Direction::Undirected:
      buffer << "Undirected";
      break;
    case Direction::LeftOrRight:
      buffer << "LeftOrRight";
      break;
    case Direction::LeftOrUndirected:
      buffer << "LeftOrUndirected";
      break;
    case Direction::UndirectedOrRight:
      buffer << "UndirectedOrRight";
      break;
    case Direction::Any:
      buffer << "Any";
      break;
  }

  if (children[label_expression_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "LABEL_EXPRESSION\n";
    children[label_expression_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[quantifier_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "QUANTIFIER\n";
    children[quantifier_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[property_map_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "PROPERTY_MAP\n";
    children[property_map_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[where_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "WHERE\n";
    children[where_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLEdgePatternNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLEdgePatternNode &>(rhs);
  return element_variable == rhs_typed.element_variable && direction == rhs_typed.direction;
}

void GQLEdgePatternNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(element_variable.size());
  state.update(element_variable);
  state.update(static_cast<size_t>(direction));
}

QueryTreeNodePtr GQLEdgePatternNode::cloneImpl() const {
  auto result = std::make_shared<GQLEdgePatternNode>();
  result->element_variable = element_variable;
  result->direction = direction;
  return result;
}

ASTPtr GQLEdgePatternNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLEdgePatternNode::toASTImpl is not implemented yet");
}

}
