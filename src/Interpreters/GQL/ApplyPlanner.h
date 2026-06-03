#pragma once

#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>

#include <string_view>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class PlanScope;

struct CorrelatedSourceContext
{
    const PlanScope & outer_scope;
    PlanScope & subquery_scope;
    std::string_view context_name;
};

void planCorrelatedSourceClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    const CorrelatedSourceContext & apply_context);

}
