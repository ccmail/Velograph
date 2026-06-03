#pragma once

#include <Interpreters/Context_fwd.h>
#include <Interpreters/GQL/SourcePlanner.h>
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

struct SelectSourceListEntry
{
    ASTPtr graph_reference;
    SourceClauseKind source_kind = SourceClauseKind::None;
    const OPENGQL::AST::GQLMatchClause * match = nullptr;
};

using SelectSourceListEntries = std::vector<SelectSourceListEntry>;

SelectSourceListEntries classifySelectSourceList(const OPENGQL::AST::GQLSelectSourceList & source_list);
bool isSameGraphMatchSourceList(const SelectSourceListEntries & entries);
bool hasDifferentGraphReferences(const SelectSourceListEntries & entries);

void planSelectSourceList(
    QueryPlan & plan,
    const OPENGQL::AST::GQLSelectSourceList & source_list,
    ContextPtr context,
    PlanScope & scope);

}
