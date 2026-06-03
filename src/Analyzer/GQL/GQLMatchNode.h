#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>
#include <Parsers/graph/AST/Utils.h>

namespace DB
{

namespace OPENGQL::AST
{
enum class GraphMatchMode : UInt8;
}

/** GQL Match Node represents a MATCH clause in GQL query.
 *
 * Corresponds to parser AST GQLMatchClause.
 *
 * Example: MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE n.age > 18 YIELD n, r, m
 */
class GQLMatchNode final : public IQueryTreeNode {
 public:
  GQLMatchNode();

  /// Returns true if this is OPTIONAL MATCH
  bool isOptional() const { return optional; }

  /// Set optional flag
  void setOptional(bool optional_value) { optional = optional_value; }

  /// Get match mode
  OPENGQL::AST::GraphMatchMode getMatchMode() const { return match_mode; }

  /// Set match mode
  void setMatchMode(OPENGQL::AST::GraphMatchMode match_mode_value) { match_mode = match_mode_value; }

  /// Get path patterns
  const ListNode &getPathPatterns() const { return children[path_patterns_child_index]->as<const ListNode &>(); }

  /// Get path patterns
  ListNode &getPathPatterns() { return children[path_patterns_child_index]->as<ListNode &>(); }

  /// Get path patterns node
  const QueryTreeNodePtr &getPathPatternsNode() const { return children[path_patterns_child_index]; }

  /// Get path patterns node
  QueryTreeNodePtr &getPathPatternsNode() { return children[path_patterns_child_index]; }

  /// Get WHERE predicate
  const QueryTreeNodePtr &getWhere() const { return children[where_child_index]; }

  /// Get WHERE predicate
  QueryTreeNodePtr &getWhere() { return children[where_child_index]; }

  /// Get KEEP clause
  const QueryTreeNodePtr &getKeep() const { return children[keep_child_index]; }

  /// Get KEEP clause
  QueryTreeNodePtr &getKeep() { return children[keep_child_index]; }

  /// Get YIELD items
  const QueryTreeNodePtr &getYield() const { return children[yield_child_index]; }

  /// Get YIELD items
  QueryTreeNodePtr &getYield() { return children[yield_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_MATCH; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  bool optional = false;
  OPENGQL::AST::GraphMatchMode match_mode = OPENGQL::AST::GraphMatchMode::None;

  static constexpr size_t path_patterns_child_index = 0;
  static constexpr size_t where_child_index = 1;
  static constexpr size_t keep_child_index = 2;
  static constexpr size_t yield_child_index = 3;
  static constexpr size_t children_size = yield_child_index + 1;
};

}
