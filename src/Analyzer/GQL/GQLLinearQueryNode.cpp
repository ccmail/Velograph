#include <Analyzer/GQL/GQLLinearQueryNode.h>

#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLFilterNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/GQL/GQLOrderByNode.h>
#include <Analyzer/GQL/GQLPageNode.h>
#include <Analyzer/ListNode.h>
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

GQLLinearQueryNode::GQLLinearQueryNode()
    : IQueryTreeNode(children_size)
{
    children[steps_child_index] = std::make_shared<ListNode>();
}

void GQLLinearQueryNode::dumpTreeImpl(WriteBuffer & buffer, FormatState & format_state, size_t indent) const
{
    buffer << std::string(indent, ' ') << "GQL_LINEAR_QUERY id: " << format_state.getNodeId(this);

    if (hasAlias())
        buffer << ", alias: " << getAlias();

    buffer << '\n' << std::string(indent + 2, ' ') << "STEPS\n";
    getStepsNode()->dumpTreeImpl(buffer, format_state, indent + 4);
}

bool GQLLinearQueryNode::isEqualImpl([[maybe_unused]] const IQueryTreeNode & rhs, CompareOptions) const
{
    return true;
}

void GQLLinearQueryNode::updateTreeHashImpl(HashState &, CompareOptions) const
{
}

QueryTreeNodePtr GQLLinearQueryNode::cloneImpl() const
{
    return std::make_shared<GQLLinearQueryNode>();
}

ASTPtr GQLLinearQueryNode::toASTImpl(const ConvertToASTOptions & options) const
{
    namespace GAST = DB::OPENGQL::AST;

    auto single_query = make_intrusive<GAST::GQLSingleQuery>();

    // Convert each step back to AST clause
    const auto & steps = getSteps().getNodes();
    single_query->clauses.reserve(steps.size());

    for (const auto & step : steps)
    {
        if (!step)
            continue;

        // Convert each QueryTree node back to its corresponding AST node
        auto clause_ast = step->toAST(options);
        if (clause_ast)
        {
            single_query->clauses.push_back(clause_ast);
            single_query->children.push_back(clause_ast);
        }
    }

    return single_query;
}

}
