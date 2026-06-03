#pragma once

#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>
#include <Parsers/graph/fwd_decl.h>

#include <vector>

namespace DB
{
class QueryPlan;
}

namespace DB::GQL
{

class PlanScope;

enum class SourceClauseKind
{
    None,
    Match,
    SelectWithSource,
};

void planUseClause(const OPENGQL::AST::GQLUseClause & use, PlanScope & scope);

SourceClauseKind classifySourceIntroducingClause(const ASTPtr & clause);
const char * sourceClauseKindName(SourceClauseKind kind);
bool isSourceIntroducingClause(const ASTPtr & clause);

class SourceClauseBuffer final
{
public:
    bool tryAppend(const ASTPtr & clause);
    bool hasPending() const;
    void flush(QueryPlan & plan, ContextPtr context, PlanScope & scope);

private:
    std::vector<const OPENGQL::AST::GQLMatchClause *> match_clauses;
};

bool tryPlanStandaloneSourceClause(
    QueryPlan & plan,
    const ASTPtr & clause,
    ContextPtr context,
    PlanScope & scope);

}
