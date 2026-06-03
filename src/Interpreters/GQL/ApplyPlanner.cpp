#include <Interpreters/GQL/ApplyPlanner.h>

#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/GQL/SourcePlanner.h>
#include <Common/Exception.h>

namespace DB::ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{

void planCorrelatedSourceClause(
    QueryPlan &,
    const ASTPtr & clause,
    ContextPtr,
    const CorrelatedSourceContext & apply_context)
{
    const auto source_kind = classifySourceIntroducingClause(clause);
    if (source_kind == SourceClauseKind::None)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "{} expected a source-introducing clause", apply_context.context_name);

    if (&apply_context.outer_scope == &apply_context.subquery_scope)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "{} apply context must separate outer and subquery scopes", apply_context.context_name);

    throw Exception(
        ErrorCodes::NOT_IMPLEMENTED,
        "{} {} source clause requires correlated apply semantics",
        apply_context.context_name,
        sourceClauseKindName(source_kind));
}

}
