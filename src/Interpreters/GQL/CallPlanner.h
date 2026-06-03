#pragma once

#include <Interpreters/Context_fwd.h>
#include <Parsers/graph/fwd_decl.h>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class PlanScope;

bool tryPlanStandaloneCallClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope);

bool tryPlanPostSourceCallClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope);

}
