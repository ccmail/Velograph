#pragma once

#include <Analyzer/IQueryTreePass.h>

namespace DB::GQL
{

/** Resolves GQL identifiers into columns referencing source bindings.
  *
  * GQL identifiers in projection clauses (currently `RETURN`) start as
  * `IdentifierNode`s produced by the builder. This pass walks each linear query in
  * clause order, collects the variable bindings introduced by source clauses
  * (currently `MATCH`), and rewrites identifiers that reference those bindings into
  * `ColumnNode`s whose source is the producing `GQLMatchNode`.
  *
  * Identifiers that do not match any visible binding are left untouched so the planner
  * can fail closed on them.
  */
class GQLNameResolutionPass final : public IQueryTreePass
{
public:
    String getName() override { return "GQLNameResolution"; }

    String getDescription() override { return "Resolve GQL identifiers into source-binding columns and resolve function nodes"; }

    void run(QueryTreeNodePtr & query_tree_node, ContextPtr context) override;
};

}
