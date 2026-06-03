#include <Analyzer/GQL/GQLPathAlternationNode.h>

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

GQLPathAlternationNode::GQLPathAlternationNode() : IQueryTreeNode(children_size) {
  children[alternatives_child_index] = std::make_shared<ListNode>();
}

void GQLPathAlternationNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PATH_ALTERNATION id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  buffer << '\n' << std::string(indent + 2, ' ') << "ALTERNATIVES\n";
  getAlternativesNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLPathAlternationNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLPathAlternationNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLPathAlternationNode::cloneImpl() const { return std::make_shared<GQLPathAlternationNode>(); }

ASTPtr GQLPathAlternationNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLPathAlternationNode::toASTImpl is not implemented yet");
}

}
