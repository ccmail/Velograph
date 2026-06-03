#include <Analyzer/GQL/GQLOrderByNode.h>

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

GQLOrderByNode::GQLOrderByNode() : IQueryTreeNode(children_size) { children[sort_items_child_index] = std::make_shared<ListNode>(); }

void GQLOrderByNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_ORDER_BY id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  buffer << '\n' << std::string(indent + 2, ' ') << "SORT_ITEMS\n";
  getSortItemsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLOrderByNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLOrderByNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLOrderByNode::cloneImpl() const { return std::make_shared<GQLOrderByNode>(); }

ASTPtr GQLOrderByNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLOrderByNode::toASTImpl is not implemented yet");
}

}
