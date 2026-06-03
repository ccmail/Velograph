#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class PlanScope;

// Legacy AST-based interface (will be deprecated)
void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const IAST & query,
    ContextPtr context);

void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const IAST & query,
    ContextPtr context,
    PlanScope & scope);

// New QueryTree-based interface (preferred)
void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const QueryTreeNodePtr & query_tree,
    ContextPtr context);

void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const QueryTreeNodePtr & query_tree,
    ContextPtr context,
    PlanScope & scope);

}
