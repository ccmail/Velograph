#include <Analyzer/GQL/GQLSimplifiedPathPatternNode.h>

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

GQLSimplifiedPathPatternNode::GQLSimplifiedPathPatternNode() : IQueryTreeNode(children_size) {}

void GQLSimplifiedPathPatternNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_SIMPLIFIED_PATH_PATTERN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (children[expression_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "EXPRESSION\n";
    children[expression_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLSimplifiedPathPatternNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLSimplifiedPathPatternNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLSimplifiedPathPatternNode::cloneImpl() const { return std::make_shared<GQLSimplifiedPathPatternNode>(); }

ASTPtr GQLSimplifiedPathPatternNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLSimplifiedPathPatternNode::toASTImpl is not implemented yet");
}

}
