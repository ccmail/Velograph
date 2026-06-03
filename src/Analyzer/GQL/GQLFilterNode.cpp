#include <Analyzer/GQL/GQLFilterNode.h>

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
extern const int LOGICAL_ERROR;
}

GQLFilterNode::GQLFilterNode() : IQueryTreeNode(children_size) {}

void GQLFilterNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_FILTER id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (children[predicate_child_index]) {
    buffer << '\n' << std::string(indent + 2, ' ') << "PREDICATE\n";
    children[predicate_child_index]->dumpTreeImpl(buffer, format_state, indent + 4);
  }
}

bool GQLFilterNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode &rhs, CompareOptions) const { return true; }

void GQLFilterNode::updateTreeHashImpl(HashState &, CompareOptions) const {}

QueryTreeNodePtr GQLFilterNode::cloneImpl() const { return std::make_shared<GQLFilterNode>(); }

ASTPtr GQLFilterNode::toASTImpl(const ConvertToASTOptions & options) const {
  namespace GAST = DB::OPENGQL::AST;

  auto where_clause = make_intrusive<GAST::GQLWhereClause>();

  // Convert predicate expression
  if (children[predicate_child_index]) {
    where_clause->predicate = children[predicate_child_index]->toAST(options);
    where_clause->children.push_back(where_clause->predicate);
  } else {
    throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL WHERE clause has no predicate");
  }

  return where_clause;
}

}
