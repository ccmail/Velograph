#include <Analyzer/GQL/GQLReturnNode.h>

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

GQLReturnNode::GQLReturnNode() : IQueryTreeNode(children_size) { children[items_child_index] = std::make_shared<ListNode>(); }

void GQLReturnNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_RETURN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (distinct) buffer << ", distinct: true";

  buffer << '\n' << std::string(indent + 2, ' ') << "ITEMS\n";
  getItemsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLReturnNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLReturnNode &>(rhs);
  return distinct == rhs_typed.distinct;
}

void GQLReturnNode::updateTreeHashImpl(HashState &state, CompareOptions) const { state.update(distinct); }

QueryTreeNodePtr GQLReturnNode::cloneImpl() const {
  auto result = std::make_shared<GQLReturnNode>();
  result->distinct = distinct;
  return result;
}

ASTPtr GQLReturnNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLReturnNode::toASTImpl is not implemented yet");
}

}
