#include <Analyzer/GQL/GQLPropertyMapNode.h>

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

GQLPropertyMapNode::GQLPropertyMapNode() : IQueryTreeNode(children_size) { children[items_child_index] = std::make_shared<ListNode>(); }

void GQLPropertyMapNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PROPERTY_MAP id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  buffer << '\n' << std::string(indent + 2, ' ') << "ITEMS\n";
  getItemsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLPropertyMapNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLPropertyMapNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLPropertyMapNode::cloneImpl() const { return std::make_shared<GQLPropertyMapNode>(); }

ASTPtr GQLPropertyMapNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLPropertyMapNode::toASTImpl is not implemented yet");
}

}
