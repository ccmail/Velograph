#include <Analyzer/GQL/GQLPropertyItemNode.h>

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

GQLPropertyItemNode::GQLPropertyItemNode() : IQueryTreeNode(children_size) {}

void GQLPropertyItemNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PROPERTY_ITEM id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (!property_name.empty()) buffer << ", property_name: " << property_name;

  if (children[value_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "VALUE\n";
    children[value_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLPropertyItemNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLPropertyItemNode &>(rhs);
  return property_name == rhs_typed.property_name;
}

void GQLPropertyItemNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(property_name.size());
  state.update(property_name);
}

QueryTreeNodePtr GQLPropertyItemNode::cloneImpl() const {
  auto result = std::make_shared<GQLPropertyItemNode>();
  result->property_name = property_name;
  return result;
}

ASTPtr GQLPropertyItemNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLPropertyItemNode::toASTImpl is not implemented yet");
}

}
