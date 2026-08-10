#include <Analyzer/GQL/GQLReturnNode.h>

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
}

GQLReturnNode::GQLReturnNode() : IQueryTreeNode(children_size) { children[items_child_index] = std::make_shared<ListNode>(); }

void GQLReturnNode::dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const {
  buffer << std::string(indent, ' ') << "GQL_RETURN id: " << format_state.getNodeId(this);

  if (hasAlias()) buffer << ", alias: " << getAlias();

  if (distinct) buffer << ", distinct: true";

  buffer << '\n' << std::string(indent + 2, ' ') << "ITEMS\n";
  getItemsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLReturnNode::isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const {
  const auto &rhs_typed = assert_cast<const GQLReturnNode &>(rhs);
  return distinct == rhs_typed.distinct;
}

void GQLReturnNode::updateTreeHashImpl(HashState &state, CompareOptions) const { state.update(distinct); }

QueryTreeNodePtr GQLReturnNode::cloneImpl() const {
  auto result = std::make_shared<GQLReturnNode>();
  result->distinct = distinct;
  return result;
}

ASTPtr GQLReturnNode::toASTImpl(const ConvertToASTOptions &options) const {
  namespace GAST = DB::OPENGQL::AST;

  auto return_clause = make_intrusive<GAST::GQLReturnClause>();
  return_clause->distinct = distinct;

  for (const auto &item : getItems().getNodes()) {
    if (!item) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL RETURN item is null");

    auto expression = item->toAST(options);
    const String alias = item->getAlias();
    expression->setAlias({});
    auto aliased_item = make_intrusive<GAST::GQLAliasedItem>(expression, alias);
    return_clause->items.push_back(aliased_item);
    return_clause->children.push_back(aliased_item);
  }

  return return_clause;
}

}
