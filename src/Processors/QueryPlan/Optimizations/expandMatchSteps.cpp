#include <Processors/QueryPlan/Optimizations/expandMatchSteps.h>

#include <Common/Exception.h>
#include <Common/typeid_cast.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>
#include <Processors/QueryPlan/Graph/MatchStep.h>
#include <Processors/QueryPlan/Graph/MatchVertexStep.h>

#include <stack>

namespace DB
{
namespace ErrorCodes
{
extern const int NOT_IMPLEMENTED;
extern const int LOGICAL_ERROR;
}
}

namespace DB::QueryPlanOptimizations
{

namespace
{

/// Validate that the MatchSpec represents a single-point pattern `(n)`.
/// Returns the variable name of the single node. Throws NOT_IMPLEMENTED
/// for any pattern shape not supported in M1.
const String & validateSinglePointPattern(const Graph::MatchSpec & spec)
{
    if (spec.clauses.size() != 1)
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED,
            "GQL MATCH with multiple clauses is not supported");

    const auto & clause = spec.clauses[0];

    if (clause.optional)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "OPTIONAL MATCH is not supported");
    if (clause.has_match_mode)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH mode is not supported");
    if (clause.has_keep_clause)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL KEEP clause is not supported");
    if (clause.has_optional_operand_block)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL optional operand blocks are not supported");
    if (clause.has_yield_items)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH YIELD is not supported");

    if (clause.paths.size() != 1)
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED,
            "GQL MATCH with multiple path patterns is not supported");

    const auto & path = clause.paths[0];

    if (path.alternation_kind != Graph::MatchPathAlternationKind::None || !path.alternatives.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL path alternation is not supported");
    if (path.prefix)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL path prefix is not supported");
    if (!path.variable.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL path variable is not supported");

    if (path.nodes.size() != 1 || !path.edges.empty())
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED,
            "GQL MATCH currently supports only single-node patterns '(n)'; "
            "edge and multi-node patterns are not supported yet");

    const auto & node = path.nodes.front();
    if (node.label_expression || node.properties || node.predicate)
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED,
            "GQL node label/property/predicate constraints are not supported yet");

    if (node.variable.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL anonymous node in single-point MATCH is not supported");

    return node.variable;
}

/// Build the referenced_columns list from the MatchStep's output header.
Names extractReferencedColumns(const Graph::MatchStep & step)
{
    Names columns;
    const auto & header = *step.getOutputHeader();
    columns.reserve(header.columns());
    for (const auto & col : header)
        columns.push_back(col.name);
    return columns;
}

} // namespace

void expandMatchSteps(QueryPlan::Node & root, QueryPlan::Nodes & /*nodes*/)
{
    std::stack<QueryPlan::Node *> stack;
    stack.push(&root);

    while (!stack.empty())
    {
        auto * node = stack.top();
        stack.pop();

        if (auto * match_step = typeid_cast<Graph::MatchStep *>(node->step.get()))
        {
            const auto & spec = match_step->getMatchSpec();
            const auto & storage = match_step->getStorage();

            if (!storage)
                throw Exception(
                    ErrorCodes::LOGICAL_ERROR,
                    "MatchStep reached expansion without a resolved graph storage");

            const auto & variable = validateSinglePointPattern(spec);
            auto referenced_columns = extractReferencedColumns(*match_step);

            node->step = std::make_unique<Graph::MatchVertexStep>(
                storage,
                variable,
                std::move(referenced_columns),
                match_step->getContext());
        }

        for (auto * child : node->children)
            stack.push(child);
    }
}

}
