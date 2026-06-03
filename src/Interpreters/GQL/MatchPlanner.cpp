#include <Interpreters/GQL/MatchPlanner.h>

#include <memory>
#include <utility>

#include <Interpreters/GQL/ClausePlanner.h>
#include <Interpreters/GQL/MatchSpecBuilder.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/GQL/PatternBinder.h>
#include <Parsers/IAST.h>
#include <Processors/QueryPlan/Graph/MatchStep.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Storages/Graph/IGraphStorage_fwd.h>
#include <Common/Exception.h>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace GQL
{
namespace
{

void validateExecutableMatch(const Graph::MatchSpec & match)
{
    if (match.optional || match.has_optional_operand_block)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "OPTIONAL MATCH is not supported");
    if (match.paths.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH must contain at least one path pattern");
}

GraphStoragePtr resolveGraphStorage(const Graph::MatchSpec & match_spec, ContextPtr context)
{
    /// A bound graph reference would be resolved here, mirroring how SQL storages are
    /// resolved: derive a StorageID from `match_spec.graph_reference`, look it up
    /// through `DatabaseCatalog`, and downcast the resulting `StoragePtr` to
    /// `IGraphStorage`. No real graph storage is registered yet, so resolution stays
    /// fail-closed and returns null; `MatchStep` then falls back to an empty
    /// placeholder source.
    (void)match_spec;
    (void)context;
    return nullptr;
}

}

void planMatchClauseSequence(
    QueryPlan & plan,
    const std::vector<const OPENGQL::AST::GQLMatchClause *> & matches,
    ContextPtr context,
    PlanScope & scope)
{
    std::vector<MatchPlan> match_plans;
    match_plans.reserve(matches.size());
    for (const auto * match : matches)
    {
        if (!match)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH clause node is null");

        match_plans.push_back(bindMatchClause(*match));
    }

    auto match_spec = buildMatchSpec(match_plans);
    if (scope.getActiveGraph())
        match_spec.graph_reference = scope.getActiveGraph()->clone();

    validateExecutableMatch(match_spec);

    auto graph_storage = resolveGraphStorage(match_spec, context);
    plan.addStep(std::make_unique<Graph::MatchStep>(std::move(match_spec), std::move(graph_storage), context));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Source);

    for (const auto & match_plan : match_plans)
    {
        if (match_plan.where)
            planWhereClause(plan, *match_plan.where, context, "GQL MATCH", scope);
    }
}

}

}
