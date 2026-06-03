#pragma once

#include <Interpreters/Context_fwd.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Parsers/graph/fwd_decl.h>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class GQLPlanBuilder final
{
public:
    explicit GQLPlanBuilder(ContextPtr context_);
    GQLPlanBuilder(ContextPtr context_, PlanScope scope_);

    void buildSingleQuery(QueryPlan & plan, const OPENGQL::AST::GQLSingleQuery & query);
    const PlanScope & getScope() const { return scope; }

private:
    ContextPtr context;
    PlanScope scope;
};

}
