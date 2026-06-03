#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Path Alternation Node represents path alternation (path1 | path2 | ...).
 *
 * Example: (a)-[r]->(b) | (a)-[s]->(c)
 */
class GQLPathAlternationNode final : public IQueryTreeNode {
 public:
  GQLPathAlternationNode();

  /// Get alternatives (list of GQLPathTermNode or nested GQLPathAlternationNode)
  const ListNode &getAlternatives() const { return children[alternatives_child_index]->as<const ListNode &>(); }

  /// Get alternatives
  ListNode &getAlternatives() { return children[alternatives_child_index]->as<ListNode &>(); }

  /// Get alternatives node
  const QueryTreeNodePtr &getAlternativesNode() const { return children[alternatives_child_index]; }

  /// Get alternatives node
  QueryTreeNodePtr &getAlternativesNode() { return children[alternatives_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PATH_ALTERNATION; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t alternatives_child_index = 0;
  static constexpr size_t children_size = alternatives_child_index + 1;
};

}
