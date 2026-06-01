#pragma once

#include <Analyzer/IQueryTreePass.h>
#include <Interpreters/Context_fwd.h>

namespace DB::GQL
{

/** Pass manager for GQL QueryTree passes.
  *
  * Mirrors the SQL `QueryTreePassManager` structure but stays isolated from the SQL pass
  * pipeline and its QueryNode-shaped validation, since GQL QueryTree roots
  * (`GQLLinearQueryNode`, `GQLCombinedQueryNode`, ...) are not SQL `QueryNode`-shaped.
  *
  * Passes are ordered as: resolution passes first (they must not change the output header
  * and are the ones `runOnlyResolve` runs), then optimization passes (skipped by
  * `runOnlyResolve`).
  */
class GQLQueryTreePassManager
{
public:
    explicit GQLQueryTreePassManager(ContextPtr context_);

    void addPass(QueryTreePassPtr pass);

    /// Run all installed passes.
    void run(QueryTreeNodePtr & query_tree_node);

    /// Run passes in range [0, up_to_pass_index).
    void run(QueryTreeNodePtr & query_tree_node, size_t up_to_pass_index);

    /// Run only the resolution passes (which must not change the output header), skipping
    /// optimization passes. Used for views / secondary queries / header-stable analysis.
    void runOnlyResolve(QueryTreeNodePtr & query_tree_node);

    size_t getPassesCount() const { return passes.size(); }

    /// Install the standard GQL analysis pipeline.
    static void addDefaultPasses(GQLQueryTreePassManager & manager);

private:
    ContextPtr context;
    QueryTreePasses passes;

    /// Number of leading resolution passes; the boundary used by `runOnlyResolve`. Tracked
    /// explicitly instead of the SQL manager's hard-coded magic index, so reordering passes
    /// cannot silently break `runOnlyResolve`.
    size_t resolution_passes_count = 0;
};

}
