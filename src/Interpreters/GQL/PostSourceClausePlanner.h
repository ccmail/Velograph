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

/// Builds a clause that appears after a source step already exists.
///
/// This dispatch layer keeps catalog, DML, and inline `CALL` out of the pure
/// `ClausePlanner` transform helper.
void planPostSourceClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope);

}
