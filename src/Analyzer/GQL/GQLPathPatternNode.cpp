#include <Analyzer/GQL/GQLPathPatternNode.h>

#include <Common/assert_cast.h>
#include <Common/SipHash.h>
#include <IO/WriteBuffer.h>
#include <IO/Operators.h>
#include <Parsers/graph/GraphAST.h>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int UNSUPPORTED_METHOD;
}

GQLPathPatternNode::GQLPathPatternNode() : IQueryTreeNode(children_size) {}

void GQLPathPatternNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_PATH_PATTERN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (!path_variable.empty()) buffer << ", path_variable: " << path_variable;

  if (!prefix.empty()) buffer << ", prefix: " << prefix;

  if (children[expression_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "EXPRESSION\n";
    children[expression_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLPathPatternNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLPathPatternNode &>(rhs);
  return path_variable == rhs_typed.path_variable && prefix == rhs_typed.prefix;
}

void GQLPathPatternNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(path_variable.size());
  state.update(path_variable);
  state.update(prefix.size());
  state.update(prefix);
}

QueryTreeNodePtr GQLPathPatternNode::cloneImpl() const {
  auto result = std::make_shared<GQLPathPatternNode>();
  result->path_variable = path_variable;
  result->prefix = prefix;
  return result;
}

ASTPtr GQLPathPatternNode::toASTImpl(const ConvertToASTOptions &options) const {
  namespace GAST = DB::OPENGQL::AST;

  if (!prefix.empty()) throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQL path prefix cannot be converted to AST yet");

  auto path_pattern = make_intrusive<GAST::GQLPathPattern>();
  if (!path_variable.empty()) {
    path_pattern->variable = GAST::GQLExpr::identifier(path_variable);
    path_pattern->children.push_back(path_pattern->variable);
  }

  if (!getExpression()) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL path pattern has no expression");

  path_pattern->expression = getExpression()->toAST(options);
  path_pattern->children.push_back(path_pattern->expression);

  return path_pattern;
}

}
