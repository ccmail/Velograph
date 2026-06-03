#include <Analyzer/GQL/GQLPathTermNode.h>

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

GQLPathTermNode::GQLPathTermNode() : IQueryTreeNode(children_size) { children[elements_child_index] = std::make_shared<ListNode>(); }

void GQLPathTermNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PATH_TERM id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  buffer << '\n' << std::string(indent + 2, ' ') << "ELEMENTS\n";
  getElementsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLPathTermNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLPathTermNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLPathTermNode::cloneImpl() const { return std::make_shared<GQLPathTermNode>(); }

ASTPtr GQLPathTermNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLPathTermNode::toASTImpl is not implemented yet");
}

}
