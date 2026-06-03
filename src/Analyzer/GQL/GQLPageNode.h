#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/ListNode.h>

namespace DB
{

/** GQL Page Node represents OFFSET/LIMIT clause.
 *
 * Example: OFFSET 10 LIMIT 20
 */
class GQLPageNode final : public IQueryTreeNode {
 public:
  GQLPageNode();

  /// Get offset expression (ConstantNode or nullptr)
  const QueryTreeNodePtr &getOffset() const { return children[offset_child_index]; }

  /// Get offset expression
  QueryTreeNodePtr &getOffset() { return children[offset_child_index]; }

  /// Get limit expression (ConstantNode or nullptr)
  const QueryTreeNodePtr &getLimit() const { return children[limit_child_index]; }

  /// Get limit expression
  QueryTreeNodePtr &getLimit() { return children[limit_child_index]; }

  QueryTreeNodeType getNodeType() const override { return QueryTreeNodeType::GQL_PAGE; }

  void dumpTreeImpl(WriteBuffer &buffer, FormatState &format_state, size_t indent) const override;

 protected:
  bool isEqualImpl(const IQueryTreeNode &rhs, CompareOptions) const override;

  void updateTreeHashImpl(HashState &hash_state, CompareOptions) const override;

  QueryTreeNodePtr cloneImpl() const override;

  ASTPtr toASTImpl(const ConvertToASTOptions &options) const override;

 private:
  static constexpr size_t offset_child_index = 0;
  static constexpr size_t limit_child_index = 1;
  static constexpr size_t children_size = limit_child_index + 1;
};

}
