#include <Analyzer/GQL/GQLQuantifierNode.h>

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

GQLQuantifierNode::GQLQuantifierNode() : IQueryTreeNode(children_size) {}

void GQLQuantifierNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_QUANTIFIER id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (children[lower_bound_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "LOWER_BOUND\n";
    children[lower_bound_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[upper_bound_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "UPPER_BOUND\n";
    children[upper_bound_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLQuantifierNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLQuantifierNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLQuantifierNode::cloneImpl() const { return std::make_shared<GQLQuantifierNode>(); }

ASTPtr GQLQuantifierNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLQuantifierNode::toASTImpl is not implemented yet");
}

}
