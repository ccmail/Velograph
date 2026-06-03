#pragma once

#include <Analyzer/IQueryTreeNode.h>
#include <Interpreters/Context_fwd.h>
#include <Parsers/IAST_fwd.h>

namespace DB::GQL
{

/** Build GQL QueryTree from GQL Parser AST.
 *
 * This is the GQL counterpart of buildQueryTree() for SQL queries.
 * It converts GQL-specific AST nodes into GQL QueryTree nodes:
 *
 * - GQLSingleQuery -> GQLLinearQueryNode
 * - GQLCombinedQuery -> GQLCombinedQueryNode
 * - GQLMatchClause -> GQLMatchNode
 * - GQLWhereClause -> GQLFilterNode
 * - GQLReturnClause -> GQLReturnNode
 * - etc.
 *
 * The resulting QueryTree can be analyzed, optimized, and then converted
 * to a QueryPlan for execution.
 *
 * Example:
 *   ASTPtr ast = ParserGQLQuery::parse("MATCH (n) RETURN n");
 *   QueryTreeNodePtr tree = buildGQLQueryTree(*ast, context);
 *   // tree is a GQLLinearQueryNode with GQLMatchNode and GQLReturnNode steps
 */
QueryTreeNodePtr buildGQLQueryTree(const IAST & query, ContextPtr context);

}
