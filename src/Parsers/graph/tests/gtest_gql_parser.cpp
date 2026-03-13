#include "config.h"

#if USE_GQL_GRAMMAR

#include <gtest/gtest.h>

#include <Parsers/graph/GQLParsingUtil.h>
#include <Parsers/graph/ASTGraphQuery.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTFunction.h>
#include <IO/WriteBufferFromString.h>

using namespace DB;

// ==================== Basic Node Pattern ====================

TEST(GQLParser, SimpleNodeScan)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (n:Person)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query, nullptr);
    ASSERT_NE(query->match_pattern, nullptr);
    ASSERT_EQ(query->match_pattern->children.size(), 1);

    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->children.size(), 1);

    auto * node = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->variable, "n");
    EXPECT_EQ(node->label, "Person");
}

TEST(GQLParser, NodeWithoutLabel)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (x)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query, nullptr);
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    ASSERT_NE(path, nullptr);

    auto * node = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->variable, "x");
    EXPECT_TRUE(node->label.empty());
}

// ==================== Edge Pattern ====================

TEST(GQLParser, RightEdge)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a:Person)-[e:KNOWS]->(b:Person)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->children.size(), 3);

    auto * node_a = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node_a, nullptr);
    EXPECT_EQ(node_a->variable, "a");
    EXPECT_EQ(node_a->label, "Person");

    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->variable, "e");
    EXPECT_EQ(edge->label, "KNOWS");
    EXPECT_EQ(edge->direction, GraphEdgeDirection::RIGHT);

    auto * node_b = path->children[2]->as<ASTNodePattern>();
    ASSERT_NE(node_b, nullptr);
    EXPECT_EQ(node_b->variable, "b");
    EXPECT_EQ(node_b->label, "Person");
}

TEST(GQLParser, LeftEdge)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a:Person)<-[e:FOLLOWS]-(b:Person)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->direction, GraphEdgeDirection::LEFT);
    EXPECT_EQ(edge->label, "FOLLOWS");
}

TEST(GQLParser, MultiHopPath)
{
    auto result = GQLParsingUtil::parseMatchQuery(
        "MATCH (a:Person)-[e1:FOLLOWS]->(b:Person)-[e2:WORKS_AT]->(c:Company)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->children.size(), 5);

    EXPECT_NE(path->children[0]->as<ASTNodePattern>(), nullptr);
    EXPECT_NE(path->children[1]->as<ASTEdgePattern>(), nullptr);
    EXPECT_NE(path->children[2]->as<ASTNodePattern>(), nullptr);
    EXPECT_NE(path->children[3]->as<ASTEdgePattern>(), nullptr);
    EXPECT_NE(path->children[4]->as<ASTNodePattern>(), nullptr);

    EXPECT_EQ(path->children[4]->as<ASTNodePattern>()->label, "Company");
}

// ==================== Label Expression ====================

TEST(GQLParser, LabelConjunction)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (n:Person&Employee)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * node = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->label_expression, nullptr);
    EXPECT_EQ(node->label_expression->op, GraphLabelOp::CONJUNCTION);
    ASSERT_EQ(node->label_expression->children.size(), 2);

    auto * left = node->label_expression->children[0]->as<ASTLabelExpression>();
    auto * right = node->label_expression->children[1]->as<ASTLabelExpression>();
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->label_name, "Person");
    EXPECT_EQ(right->label_name, "Employee");
}

TEST(GQLParser, LabelDisjunction)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (n:Person|Company)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * node = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->label_expression, nullptr);
    EXPECT_EQ(node->label_expression->op, GraphLabelOp::DISJUNCTION);
}

TEST(GQLParser, LabelNegation)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (n:!Person)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * node = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->label_expression, nullptr);
    EXPECT_EQ(node->label_expression->op, GraphLabelOp::NEGATION);
}

TEST(GQLParser, LabelWildcard)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (n:%)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * node = path->children[0]->as<ASTNodePattern>();
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->label_expression, nullptr);
    EXPECT_EQ(node->label_expression->op, GraphLabelOp::WILDCARD);
}

// ==================== Path Quantifiers ====================

TEST(GQLParser, QuantifierStar)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a)-[e:KNOWS]->*(b)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(edge->quantifier, nullptr);
    EXPECT_EQ(edge->quantifier->min_hops, 0u);
    EXPECT_EQ(edge->quantifier->max_hops, ASTPathQuantifier::UNLIMITED);
}

TEST(GQLParser, QuantifierPlus)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a)-[e:KNOWS]->+(b)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(edge->quantifier, nullptr);
    EXPECT_EQ(edge->quantifier->min_hops, 1u);
    EXPECT_EQ(edge->quantifier->max_hops, ASTPathQuantifier::UNLIMITED);
}

TEST(GQLParser, QuantifierQuestion)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a)-[e:KNOWS]->?(b)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(edge->quantifier, nullptr);
    EXPECT_EQ(edge->quantifier->min_hops, 0u);
    EXPECT_EQ(edge->quantifier->max_hops, 1u);
}

TEST(GQLParser, QuantifierFixed)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a)-[e:KNOWS]->{3}(b)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(edge->quantifier, nullptr);
    EXPECT_EQ(edge->quantifier->min_hops, 3u);
    EXPECT_EQ(edge->quantifier->max_hops, 3u);
}

TEST(GQLParser, QuantifierRange)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a)-[e:KNOWS]->{2,5}(b)");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * path = query->match_pattern->children[0]->as<ASTPathPattern>();
    auto * edge = path->children[1]->as<ASTEdgePattern>();
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(edge->quantifier, nullptr);
    EXPECT_EQ(edge->quantifier->min_hops, 2u);
    EXPECT_EQ(edge->quantifier->max_hops, 5u);
}

// ==================== WHERE Clause ====================

TEST(GQLParser, WhereSimpleComparison)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a:Person) WHERE a.age > 30");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query, nullptr);
    ASSERT_NE(query->where_condition, nullptr);

    auto * func = query->where_condition->as<ASTFunction>();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "greater");
}

TEST(GQLParser, WhereEquality)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a:Person) WHERE a.name = 'Alice'");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query->where_condition, nullptr);

    auto * func = query->where_condition->as<ASTFunction>();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "equals");
}

TEST(GQLParser, WhereBooleanAnd)
{
    auto result = GQLParsingUtil::parseMatchQuery(
        "MATCH (a:Person) WHERE a.age > 20 AND a.age < 40");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query->where_condition, nullptr);

    auto * func = query->where_condition->as<ASTFunction>();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "and");
    ASSERT_EQ(func->arguments->children.size(), 2);

    auto * left = func->arguments->children[0]->as<ASTFunction>();
    auto * right = func->arguments->children[1]->as<ASTFunction>();
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->name, "greater");
    EXPECT_EQ(right->name, "less");
}

TEST(GQLParser, WherePropertyAccess)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH (a:Person) WHERE a.name = 'Bob'");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    auto * eq = query->where_condition->as<ASTFunction>();
    ASSERT_NE(eq, nullptr);
    ASSERT_EQ(eq->arguments->children.size(), 2);

    auto * prop = eq->arguments->children[0]->as<ASTFunction>();
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->name, "tupleElement");
    ASSERT_EQ(prop->arguments->children.size(), 2);

    auto * ident = prop->arguments->children[0]->as<ASTIdentifier>();
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->name(), "a");

    auto * field_name = prop->arguments->children[1]->as<ASTLiteral>();
    ASSERT_NE(field_name, nullptr);
    EXPECT_EQ(field_name->value.safeGet<String>(), "name");
}

// ==================== RETURN Clause ====================

TEST(GQLParser, ReturnStatement)
{
    auto result = GQLParsingUtil::parseGQLStatement("MATCH (a:Person) RETURN a.name AS name, a.age");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query, nullptr);
    ASSERT_NE(query->return_clause, nullptr);
    EXPECT_FALSE(query->return_clause->distinct);

    size_t return_items = 0;
    for (const auto & child : query->return_clause->children)
    {
        if (child->as<ASTGraphReturnItem>())
            ++return_items;
    }
    EXPECT_EQ(return_items, 2);
}

TEST(GQLParser, ReturnDistinct)
{
    auto result = GQLParsingUtil::parseGQLStatement("MATCH (a:Person) RETURN DISTINCT a.city");
    ASSERT_TRUE(result.ast) << result.error_message;

    auto * query = result.ast->as<ASTGraphQuery>();
    ASSERT_NE(query, nullptr);
    ASSERT_NE(query->return_clause, nullptr);
    EXPECT_TRUE(query->return_clause->distinct);
}

// ==================== AST Format Roundtrip ====================

TEST(GQLParser, FormatNodePattern)
{
    auto node = make_intrusive<ASTNodePattern>();
    node->variable = "n";
    node->label = "Person";

    WriteBufferFromOwnString buf;
    IAST::FormatSettings settings(false);
    IAST::FormatState state;
    IAST::FormatStateStacked frame;
    node->format(buf, settings, state, frame);

    EXPECT_EQ(buf.str(), "(n:Person)");
}

TEST(GQLParser, FormatEdgePattern)
{
    auto edge = make_intrusive<ASTEdgePattern>();
    edge->variable = "e";
    edge->label = "KNOWS";
    edge->direction = GraphEdgeDirection::RIGHT;

    WriteBufferFromOwnString buf;
    IAST::FormatSettings settings(false);
    IAST::FormatState state;
    IAST::FormatStateStacked frame;
    edge->format(buf, settings, state, frame);

    EXPECT_EQ(buf.str(), "-[e:KNOWS]->");
}

TEST(GQLParser, FormatLeftEdge)
{
    auto edge = make_intrusive<ASTEdgePattern>();
    edge->variable = "e";
    edge->label = "FOLLOWS";
    edge->direction = GraphEdgeDirection::LEFT;

    WriteBufferFromOwnString buf;
    IAST::FormatSettings settings(false);
    IAST::FormatState state;
    IAST::FormatStateStacked frame;
    edge->format(buf, settings, state, frame);

    EXPECT_EQ(buf.str(), "<-[e:FOLLOWS]-");
}

TEST(GQLParser, FormatQuantifierStar)
{
    auto quant = make_intrusive<ASTPathQuantifier>();
    quant->min_hops = 0;
    quant->max_hops = ASTPathQuantifier::UNLIMITED;

    WriteBufferFromOwnString buf;
    IAST::FormatSettings settings(false);
    IAST::FormatState state;
    IAST::FormatStateStacked frame;
    quant->format(buf, settings, state, frame);

    EXPECT_EQ(buf.str(), "*");
}

TEST(GQLParser, FormatQuantifierRange)
{
    auto quant = make_intrusive<ASTPathQuantifier>();
    quant->min_hops = 2;
    quant->max_hops = 5;

    WriteBufferFromOwnString buf;
    IAST::FormatSettings settings(false);
    IAST::FormatState state;
    IAST::FormatStateStacked frame;
    quant->format(buf, settings, state, frame);

    EXPECT_EQ(buf.str(), "{2,5}");
}

// ==================== Error Handling ====================

TEST(GQLParser, InvalidSyntaxReturnsError)
{
    auto result = GQLParsingUtil::parseMatchQuery("MATCH INVALID SYNTAX !!!");
    EXPECT_FALSE(result.ast);
    EXPECT_FALSE(result.error_message.empty());
}

// ==================== Clone ====================

TEST(GQLParser, CloneNodePattern)
{
    auto node = make_intrusive<ASTNodePattern>();
    node->variable = "n";
    node->label = "Person";

    auto cloned = node->clone();
    auto * cloned_node = cloned->as<ASTNodePattern>();
    ASSERT_NE(cloned_node, nullptr);
    EXPECT_EQ(cloned_node->variable, "n");
    EXPECT_EQ(cloned_node->label, "Person");
    EXPECT_NE(cloned_node, node.get());
}

TEST(GQLParser, CloneEdgeWithQuantifier)
{
    auto quant = make_intrusive<ASTPathQuantifier>();
    quant->min_hops = 1;
    quant->max_hops = 5;

    auto edge = make_intrusive<ASTEdgePattern>();
    edge->variable = "e";
    edge->label = "KNOWS";
    edge->direction = GraphEdgeDirection::RIGHT;
    edge->quantifier = quant.get();
    edge->children.push_back(quant);

    auto cloned = edge->clone();
    auto * cloned_edge = cloned->as<ASTEdgePattern>();
    ASSERT_NE(cloned_edge, nullptr);
    EXPECT_EQ(cloned_edge->variable, "e");
    ASSERT_NE(cloned_edge->quantifier, nullptr);
    EXPECT_EQ(cloned_edge->quantifier->min_hops, 1u);
    EXPECT_EQ(cloned_edge->quantifier->max_hops, 5u);
    EXPECT_NE(cloned_edge->quantifier, edge->quantifier);
}

#endif
