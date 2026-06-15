#pragma once

#include <Interpreters/Context_fwd.h>
#include <Interpreters/IInterpreter.h>
#include <Interpreters/SelectQueryOptions.h>
#include <Parsers/IAST_fwd.h>
#include <Analyzer/IQueryTreeNode.h>
#include <QueryPipeline/BlockIO.h>

namespace DB
{

class QueryPlan;

/** Interpreter for GQL queries using QueryTree-based analysis.
  *
  * This is the GQL counterpart of InterpreterSelectQueryAnalyzer:
  *
  * 1. Parser AST -> GQL QueryTree (GQLQueryTreeBuilder)
  * 2. QueryTree analysis passes (GQLQueryTreePassManager), choosing run vs runOnlyResolve
  *    from SelectQueryOptions, just like InterpreterSelectQueryAnalyzer
  * 3. QueryTree -> QueryPlan (GQLPlanner)
  * 4. QueryPlan -> QueryPipeline -> execution
  */
class InterpreterGQLQueryAnalyzer final : public IInterpreter
{
public:
    /** Construct from Parser AST: build the QueryTree and run analysis passes. */
    InterpreterGQLQueryAnalyzer(
        const ASTPtr & query_, const ContextPtr & context_, const SelectQueryOptions & select_query_options_ = {});

    /** Construct from an already-built QueryTree (e.g. a nested query). */
    InterpreterGQLQueryAnalyzer(
        const QueryTreeNodePtr & query_tree_, const ContextPtr & context_, const SelectQueryOptions & select_query_options_ = {});

    /** Execute the query and return the result pipeline. */
    BlockIO execute() override;

    /** Build the QueryPlan and return its output header without executing the pipeline. */
    SharedHeader getSampleBlock();

    /** Get the QueryTree for inspection or further processing. */
    QueryTreeNodePtr getQueryTree() const { return query_tree; }

    /** Build the QueryPlan without executing it. */
    void buildQueryPlan(QueryPlan & query_plan);

private:
    ASTPtr query;
    ContextPtr context;
    SelectQueryOptions select_query_options;
    QueryTreeNodePtr query_tree;
};

}
