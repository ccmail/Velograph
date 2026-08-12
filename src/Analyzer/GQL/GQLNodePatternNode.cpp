#include <Analyzer/GQL/GQLNodePatternNode.h>

#include <Common/assert_cast.h>
#include <Common/SipHash.h>
#include <IO/WriteBuffer.h>
#include <IO/Operators.h>
#include <Parsers/graph/GraphAST.h>

namespace DB
{

namespace ErrorCodes
{
extern const int UNSUPPORTED_METHOD;
}

GQLNodePatternNode::GQLNodePatternNode() : IQueryTreeNode(children_size) {}

void GQLNodePatternNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_NODE_PATTERN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (!element_variable.empty()) buffer << ", element_variable: " << element_variable;

  if (children[label_expression_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "LABEL_EXPRESSION\n";
    children[label_expression_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[property_map_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "PROPERTY_MAP\n";
    children[property_map_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[where_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "WHERE\n";
    children[where_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLNodePatternNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLNodePatternNode &>(rhs);
  return element_variable == rhs_typed.element_variable;
}

void GQLNodePatternNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(element_variable.size());
  state.update(element_variable);
}

QueryTreeNodePtr GQLNodePatternNode::cloneImpl() const {
  auto result = std::make_shared<GQLNodePatternNode>();
  result->element_variable = element_variable;
  return result;
}

ASTPtr GQLNodePatternNode::toASTImpl(const ConvertToASTOptions &options) const {
  namespace GAST = DB::OPENGQL::AST;

  auto node_pattern = make_intrusive<GAST::GQLNodePattern>();
  if (!element_variable.empty()) node_pattern->variable = GAST::GQLExpr::identifier(element_variable);
  if (getLabelExpression()) node_pattern->label_expression = getLabelExpression()->toAST(options);
  if (getPropertyMap()) node_pattern->properties = getPropertyMap()->toAST(options);
  if (getWhere()) node_pattern->where = make_intrusive<GAST::GQLWhereClause>(getWhere()->toAST(options));

  if (node_pattern->variable) node_pattern->children.push_back(node_pattern->variable);
  if (node_pattern->label_expression) node_pattern->children.push_back(node_pattern->label_expression);
  if (node_pattern->properties) node_pattern->children.push_back(node_pattern->properties);
  if (node_pattern->where) node_pattern->children.push_back(node_pattern->where);
  return node_pattern;
}

}
