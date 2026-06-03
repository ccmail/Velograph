#include <Analyzer/GQL/GQLOptionalBlockNode.h>

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

GQLOptionalBlockNode::GQLOptionalBlockNode() : IQueryTreeNode(children_size) {}

void GQLOptionalBlockNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_OPTIONAL_BLOCK id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (children[inner_query_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "INNER_QUERY\n";
    children[inner_query_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLOptionalBlockNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLOptionalBlockNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLOptionalBlockNode::cloneImpl() const { return std::make_shared<GQLOptionalBlockNode>(); }

ASTPtr GQLOptionalBlockNode::toASTImpl(const ConvertToASTOptions &) const {
  throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQLOptionalBlockNode::toASTImpl is not implemented yet");
}

}
