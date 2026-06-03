#include <Analyzer/GQL/GQLSimplifiedPathExprNode.h>

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

GQLSimplifiedPathExprNode::GQLSimplifiedPathExprNode() : IQueryTreeNode(children_size) {}

void GQLSimplifiedPathExprNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_SIMPLIFIED_PATH_EXPR id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (children[edge_pattern_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "EDGE_PATTERN\n";
    children[edge_pattern_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLSimplifiedPathExprNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLSimplifiedPathExprNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLSimplifiedPathExprNode::cloneImpl() const { return std::make_shared<GQLSimplifiedPathExprNode>(); }

ASTPtr GQLSimplifiedPathExprNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLSimplifiedPathExprNode::toASTImpl is not implemented yet");
}

}
