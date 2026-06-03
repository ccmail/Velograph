#include <Analyzer/GQL/GQLPageNode.h>

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

GQLPageNode::GQLPageNode() : IQueryTreeNode(children_size) {}

void GQLPageNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PAGE id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (children[offset_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "OFFSET\n";
    children[offset_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[limit_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "LIMIT\n";
    children[limit_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLPageNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLPageNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLPageNode::cloneImpl() const { return std::make_shared<GQLPageNode>(); }

ASTPtr GQLPageNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLPageNode::toASTImpl is not implemented yet");
}

}
