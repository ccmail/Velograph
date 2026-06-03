#pragma once

#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class PlanScope;

/// Builds a linear `GQLSingleQuery` clause sequence.
///
/// This is the `GQL` counterpart of a clause-query interpreter: it owns clause
/// ordering and source/post-source boundary decisions, while individual planner
/// modules keep the implementation for each clause family.
void planClauseSequence(
    QueryPlan & plan,
    const ASTs & clauses,
    ContextPtr context,
    PlanScope & scope);

}
