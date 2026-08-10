#include <Analyzer/GQL/GQLEdgePatternNode.h>

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

GQLEdgePatternNode::GQLEdgePatternNode() : IQueryTreeNode(children_size) {}

void GQLEdgePatternNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_EDGE_PATTERN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (!element_variable.empty()) buffer << ", element_variable: " << element_variable;

  buffer << ", direction: ";
  switch (direction) {
    case Direction::Left:
      buffer << "Left";
      break;
    case Direction::Right:
      buffer << "Right";
      break;
    case Direction::Undirected:
      buffer << "Undirected";
      break;
    case Direction::LeftOrRight:
      buffer << "LeftOrRight";
      break;
    case Direction::LeftOrUndirected:
      buffer << "LeftOrUndirected";
      break;
    case Direction::UndirectedOrRight:
      buffer << "UndirectedOrRight";
      break;
    case Direction::Any:
      buffer << "Any";
      break;
  }

  if (children[label_expression_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "LABEL_EXPRESSION\n";
    children[label_expression_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }

  if (children[quantifier_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "QUANTIFIER\n";
    children[quantifier_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
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

bool GQLEdgePatternNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLEdgePatternNode &>(rhs);
  return element_variable == rhs_typed.element_variable && direction == rhs_typed.direction;
}

void GQLEdgePatternNode::updateTreeHashImpl(HashState &state, CompareOptions) const {
  state.update(element_variable.size());
  state.update(element_variable);
  state.update(static_cast<size_t>(direction));
}

QueryTreeNodePtr GQLEdgePatternNode::cloneImpl() const {
  auto result = std::make_shared<GQLEdgePatternNode>();
  result->element_variable = element_variable;
  result->direction = direction;
  return result;
}

ASTPtr GQLEdgePatternNode::toASTImpl(const ConvertToASTOptions &options) const {
  namespace GAST = DB::OPENGQL::AST;

  GAST::EdgeDirection ast_direction;
  switch (direction) {
    case Direction::Left:
      ast_direction = GAST::EdgeDirection::Left;
      break;
    case Direction::Right:
      ast_direction = GAST::EdgeDirection::Right;
      break;
    case Direction::Undirected:
      ast_direction = GAST::EdgeDirection::Undirected;
      break;
    case Direction::LeftOrRight:
      ast_direction = GAST::EdgeDirection::LeftOrRight;
      break;
    case Direction::LeftOrUndirected:
      ast_direction = GAST::EdgeDirection::LeftOrUndirected;
      break;
    case Direction::UndirectedOrRight:
      ast_direction = GAST::EdgeDirection::UndirectedOrRight;
      break;
    case Direction::Any:
      ast_direction = GAST::EdgeDirection::Any;
      break;
  }

  auto edge_pattern = make_intrusive<GAST::GQLEdgePattern>(ast_direction);
  if (!element_variable.empty()) edge_pattern->variable = GAST::GQLExpr::identifier(element_variable);
  if (getLabelExpression()) edge_pattern->label_expression = getLabelExpression()->toAST(options);
  if (getPropertyMap()) edge_pattern->properties = getPropertyMap()->toAST(options);
  if (getWhere()) edge_pattern->where = make_intrusive<GAST::GQLWhereClause>(getWhere()->toAST(options));
  if (getQuantifier()) throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "GQL edge quantifier cannot be converted to AST yet");

  if (edge_pattern->variable) edge_pattern->children.push_back(edge_pattern->variable);
  if (edge_pattern->label_expression) edge_pattern->children.push_back(edge_pattern->label_expression);
  if (edge_pattern->properties) edge_pattern->children.push_back(edge_pattern->properties);
  if (edge_pattern->where) edge_pattern->children.push_back(edge_pattern->where);
  return edge_pattern;
}

}
