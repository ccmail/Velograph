#pragma once

#include <Interpreters/IInterpreter.h>
#include <Parsers/IAST_fwd.h>

namespace DB
{

class QueryPlan;

class InterpreterGQLQuery final : public IInterpreter, WithContext
{
public:
    InterpreterGQLQuery(ASTPtr query_ptr_, ContextPtr context_);

    BlockIO execute() override;

    void buildQueryPlan(QueryPlan & query_plan);

private:
    ASTPtr query_ptr;
};

}
