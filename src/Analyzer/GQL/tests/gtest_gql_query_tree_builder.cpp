#include <gtest/gtest.h>

#include <Analyzer/GQL/GQLQueryTreeBuilder.h>
#include <Analyzer/GQL/GQLLinearQueryNode.h>
#include <Analyzer/GQL/GQLCombinedQueryNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/IdentifierNode.h>
#include <Analyzer/ListNode.h>
#include <Interpreters/Context.h>
#include <Parsers/graph/ParserGQLQuery.h>
#include <Parsers/graph/GraphAST.h>

using namespace DB;
using namespace DB::GQL;

namespace
{

ContextMutablePtr getContext()
{
    static ContextMutablePtr context = Context::createGlobal(nullptr);
    return context;
}

ASTPtr parseGQL(const std::string & query)
{
    ParserGQLQuery parser;
    return parser.parseStatementText(query);
}

}

TEST(GQLQueryTreeBuilder, SimpleMatch)
{
    // Parse: MATCH (n) RETURN n
    auto ast = parseGQL("MATCH (n) RETURN n");
    ASSERT_NE(ast, nullptr);

    // Build QueryTree
    auto query_tree = buildGQLQueryTree(*ast, getContext());
    ASSERT_NE(query_tree, nullptr);

    // Verify it's a GQLLinearQueryNode
    auto * linear_node = query_tree->as<GQLLinearQueryNode>();
    ASSERT_NE(linear_node, nullptr);

    // Verify it has steps
    const auto & steps = linear_node->getSteps().getNodes();
    EXPECT_GE(steps.size(), 1u);

    // First step should be MATCH
    if (!steps.empty())
    {
        auto * match_node = steps[0]->as<GQLMatchNode>();
        EXPECT_NE(match_node, nullptr);
    }
}

TEST(GQLQueryTreeBuilder, CombinedQuery)
{
    // Parse: MATCH (n) RETURN n UNION ALL MATCH (m) RETURN m
    auto ast = parseGQL("MATCH (n) RETURN n UNION ALL MATCH (m) RETURN m");
    ASSERT_NE(ast, nullptr);

    // Build QueryTree
    auto query_tree = buildGQLQueryTree(*ast, getContext());
    ASSERT_NE(query_tree, nullptr);

    // Verify it's a GQLCombinedQueryNode
    auto * combined_node = query_tree->as<GQLCombinedQueryNode>();
    ASSERT_NE(combined_node, nullptr);

    // Verify it has 2 subqueries
    const auto & queries = combined_node->getQueries().getNodes();
    EXPECT_EQ(queries.size(), 2u);

    // Verify it has 1 operator (UNION ALL)
    const auto & operators = combined_node->getOperators();
    EXPECT_EQ(operators.size(), 1u);
    if (!operators.empty())
    {
        EXPECT_EQ(operators[0], GQLCombinedQueryNode::CombinedOperator::UNION_ALL);
    }
}

TEST(GQLQueryTreeBuilder, RoundTrip)
{
    // Parse -> QueryTree -> AST -> QueryTree should be idempotent
    auto original_ast = parseGQL("MATCH (n) RETURN n");
    ASSERT_NE(original_ast, nullptr);

    // Build QueryTree
    auto query_tree = buildGQLQueryTree(*original_ast, getContext());
    ASSERT_NE(query_tree, nullptr);

    // Convert back to AST
    auto reconstructed_ast = query_tree->toAST();
    ASSERT_NE(reconstructed_ast, nullptr);

    // Build QueryTree again
    auto query_tree2 = buildGQLQueryTree(*reconstructed_ast, getContext());
    ASSERT_NE(query_tree2, nullptr);

    // Both QueryTrees should have the same structure
    EXPECT_EQ(query_tree->getNodeType(), query_tree2->getNodeType());
}

TEST(GQLQueryTreeBuilder, MatchPatternFilled)
{
    // MATCH (n) RETURN n must lower into a fully filled pattern, not a placeholder.
    auto ast = parseGQL("MATCH (n) RETURN n");
    ASSERT_NE(ast, nullptr);

    auto query_tree = buildGQLQueryTree(*ast, getContext());
    auto * linear_node = query_tree->as<GQLLinearQueryNode>();
    ASSERT_NE(linear_node, nullptr);

    const auto & steps = linear_node->getSteps().getNodes();
    ASSERT_EQ(steps.size(), 2u);

    auto * match_node = steps[0]->as<GQLMatchNode>();
    ASSERT_NE(match_node, nullptr);

    const auto & patterns = match_node->getPathPatterns().getNodes();
    ASSERT_EQ(patterns.size(), 1u);

    auto * path_pattern = patterns[0]->as<GQLPathPatternNode>();
    ASSERT_NE(path_pattern, nullptr);

    auto * path_term = path_pattern->getExpression()->as<GQLPathTermNode>();
    ASSERT_NE(path_term, nullptr);

    const auto & elements = path_term->getElements().getNodes();
    ASSERT_EQ(elements.size(), 1u);

    auto * node_pattern = elements[0]->as<GQLNodePatternNode>();
    ASSERT_NE(node_pattern, nullptr);
    EXPECT_EQ(node_pattern->getElementVariable(), "n");

    auto * return_node = steps[1]->as<GQLReturnNode>();
    ASSERT_NE(return_node, nullptr);

    const auto & items = return_node->getItems().getNodes();
    ASSERT_EQ(items.size(), 1u);

    auto * identifier = items[0]->as<IdentifierNode>();
    ASSERT_NE(identifier, nullptr);
    EXPECT_EQ(identifier->getIdentifier().getFullName(), "n");
}

TEST(GQLQueryTreeBuilder, MatchEdgeChain)
{
    // MATCH (a)-[r]->(b) must produce an alternating node/edge/node chain with direction.
    auto ast = parseGQL("MATCH (a)-[r]->(b) RETURN a");
    ASSERT_NE(ast, nullptr);

    auto query_tree = buildGQLQueryTree(*ast, getContext());
    auto * linear_node = query_tree->as<GQLLinearQueryNode>();
    ASSERT_NE(linear_node, nullptr);

    const auto & steps = linear_node->getSteps().getNodes();
    ASSERT_GE(steps.size(), 1u);

    auto * match_node = steps[0]->as<GQLMatchNode>();
    ASSERT_NE(match_node, nullptr);

    const auto & patterns = match_node->getPathPatterns().getNodes();
    ASSERT_EQ(patterns.size(), 1u);

    auto * path_term = patterns[0]->as<GQLPathPatternNode>()->getExpression()->as<GQLPathTermNode>();
    ASSERT_NE(path_term, nullptr);

    const auto & elements = path_term->getElements().getNodes();
    ASSERT_EQ(elements.size(), 3u);

    auto * a = elements[0]->as<GQLNodePatternNode>();
    auto * edge = elements[1]->as<GQLEdgePatternNode>();
    auto * b = elements[2]->as<GQLNodePatternNode>();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(a->getElementVariable(), "a");
    EXPECT_EQ(edge->getElementVariable(), "r");
    EXPECT_EQ(b->getElementVariable(), "b");
    EXPECT_EQ(edge->getDirection(), GQLEdgePatternNode::Direction::Right);
}
