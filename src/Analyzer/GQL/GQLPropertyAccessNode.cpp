#include <Analyzer/GQL/GQLPropertyAccessNode.h>

#include <Common/assert_cast.h>
#include <Common/Exception.h>
#include <Common/SipHash.h>
#include <IO/Operators.h>
#include <IO/WriteBuffer.h>
#include <Parsers/graph/GraphAST.h>

namespace DB {

namespace ErrorCodes {
extern const int UNSUPPORTED_METHOD;
}

GQLPropertyAccessNode::GQLPropertyAccessNode() : IQueryTreeNode(children_size) {}

GQLPropertyAccessNode::GQLPropertyAccessNode(String property_name_)
    : IQueryTreeNode(children_size), property_name(std::move(property_name_)) {}

void GQLPropertyAccessNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PROPERTY_ACCESS id: " << format_state.getNodeId(this) << ", property: " << property_name;

  if (getBase()) {
    buffer << '\n' << std::string(indent + 2, ' ') << "BASE\n";
    getBase()->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLPropertyAccessNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLPropertyAccessNode &>(rhs);
  return property_name == rhs_typed.property_name;
}

void GQLPropertyAccessNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(property_name.size());
  state.update(property_name);
}

QueryTreeNodePtr GQLPropertyAccessNode::cloneImpl() const { return std::make_shared<GQLPropertyAccessNode>(property_name); }

ASTPtr GQLPropertyAccessNode::toASTImpl(const ConvertToASTOptions &options) const {
  namespace GAST = DB::OPENGQL::AST;

  if (!getBase()) throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQL property access has no base expression");
  return GAST::GQLExpr::property(getBase()->toAST(options), property_name);
}

}  // namespace DB
