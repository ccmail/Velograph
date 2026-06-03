#pragma once

#include <memory>

namespace DB
{

// Forward declarations for all GQL QueryTree nodes

// Query root nodes
class GQLLinearQueryNode;
using GQLLinearQueryNodePtr = std::shared_ptr<GQLLinearQueryNode>;

class GQLCombinedQueryNode;
using GQLCombinedQueryNodePtr = std::shared_ptr<GQLCombinedQueryNode>;

// Match and optional nodes
class GQLMatchNode;
using GQLMatchNodePtr = std::shared_ptr<GQLMatchNode>;

class GQLOptionalBlockNode;
using GQLOptionalBlockNodePtr = std::shared_ptr<GQLOptionalBlockNode>;

// Path pattern nodes
class GQLPathPatternNode;
using GQLPathPatternNodePtr = std::shared_ptr<GQLPathPatternNode>;

class GQLPathTermNode;
using GQLPathTermNodePtr = std::shared_ptr<GQLPathTermNode>;

class GQLPathAlternationNode;
using GQLPathAlternationNodePtr = std::shared_ptr<GQLPathAlternationNode>;

class GQLSimplifiedPathPatternNode;
using GQLSimplifiedPathPatternNodePtr = std::shared_ptr<GQLSimplifiedPathPatternNode>;

class GQLSimplifiedPathExprNode;
using GQLSimplifiedPathExprNodePtr = std::shared_ptr<GQLSimplifiedPathExprNode>;

// Element pattern nodes
class GQLNodePatternNode;
using GQLNodePatternNodePtr = std::shared_ptr<GQLNodePatternNode>;

class GQLEdgePatternNode;
using GQLEdgePatternNodePtr = std::shared_ptr<GQLEdgePatternNode>;

class GQLQuantifierNode;
using GQLQuantifierNodePtr = std::shared_ptr<GQLQuantifierNode>;

// Constraint nodes
class GQLLabelExpressionNode;
using GQLLabelExpressionNodePtr = std::shared_ptr<GQLLabelExpressionNode>;

class GQLPropertyMapNode;
using GQLPropertyMapNodePtr = std::shared_ptr<GQLPropertyMapNode>;

class GQLPropertyItemNode;
using GQLPropertyItemNodePtr = std::shared_ptr<GQLPropertyItemNode>;

// Projection and filter nodes
class GQLKeepNode;
using GQLKeepNodePtr = std::shared_ptr<GQLKeepNode>;

class GQLYieldNode;
using GQLYieldNodePtr = std::shared_ptr<GQLYieldNode>;

class GQLReturnNode;
using GQLReturnNodePtr = std::shared_ptr<GQLReturnNode>;

class GQLFilterNode;
using GQLFilterNodePtr = std::shared_ptr<GQLFilterNode>;

// Post-processing nodes
class GQLOrderByNode;
using GQLOrderByNodePtr = std::shared_ptr<GQLOrderByNode>;

class GQLPageNode;
using GQLPageNodePtr = std::shared_ptr<GQLPageNode>;

}
