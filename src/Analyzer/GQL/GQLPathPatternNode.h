#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Path Pattern Node represents a path pattern in MATCH clause.
 *
 * Corresponds to parser AST GQLPathPattern.
 *
 * Example: p = (a)-[r]->(b)
 * Example: ANY SHORTEST (a)-[r*1..5]->(b)
 */
class GQLPathPatternNode final : public IQueryTreeNode {
 public:
  GQLPathPatternNode();

  /// Get path variable name (e.g., 'p' in 'p = (a)-[r]->(b)')
  const String &getPathVariable() const { return path_variable; }

  /// Set path variable name
  void setPathVariable(String path_variable_value) { path_variable = std::move(path_variable_value); }

  /// Get path prefix (e.g., 'ANY SHORTEST')
  const String &getPrefix() const { return prefix; }

  /// Set path prefix
  void setPrefix(String prefix_value) { prefix = std::move(prefix_value); }

  /// Get path expression (GQLPathTermNode | GQLPathAlternationNode | GQLSimplifiedPathPatternNode)
  const QueryTreeNodePtr &getExpression() const { return children[expression_child_index]; }

  /// Get path expression
  QueryTreeNodePtr &getExpression() { return children[expression_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PATH_PATTERN; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  String path_variable;
  String prefix;

  static constexpr size_t expression_child_index = 0;
  static constexpr size_t children_size = expression_child_index + 1;
};

}
