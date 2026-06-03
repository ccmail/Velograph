#include "config.h"

#if USE_GQL_GRAMMAR

#  include <Common/Exception.h>
#  include <gtest/gtest.h>
#  include <IO/WriteBufferFromString.h>
#  include <Parsers/ASTSetQuery.h>
#  include <Parsers/graph/GraphAST.h>
#  include <Parsers/graph/GQLParserUtils.h>
#  include <Parsers/graph/ParserGQLQuery.h>
#  include <Parsers/parseQuery.h>
#  include <Parsers/ParserQuery.h>

#  include <unordered_set>

using namespace DB;

namespace GAST = DB::OPENGQL::AST;

namespace {

ASTPtr parseGQLOrThrow(std::string_view query) {
  ParserGQLQuery parser;
  const String query_string(query);
  return DB::parseGQLQuery(parser, query_string, 0, 0, 0);
}

ASTPtr parseGraphOrThrow(std::string_view query) { return parseGQLOrThrow(query); }

ASTPtr parseDMLOrThrow(std::string_view query) { return parseGQLOrThrow(query); }

ASTPtr parseTopLevelOrThrow(const String& query) {
  ParserQuery parser(query.data() + query.size());
  return DB::parseQuery(parser, query, 0, 0, 0);
}

String formatAST(const IAST& ast) {
  WriteBufferFromOwnString out;
  IAST::FormatSettings settings(false);
  IAST::FormatState state;
  ast.format(out, settings, state, {});
  return out.str();
}

const GAST::GQLSingleQuery* getSingleQuery(const ASTPtr& ast) {
  const auto* query = ast->as<GAST::GQLSingleQuery>();
  EXPECT_NE(query, nullptr);
  return query;
}

const GAST::GQLSingleQuery* getClausesQuery(const ASTPtr& ast) { return getSingleQuery(ast); }

const GAST::GQLCatalogStatement* getCatalogStatement(const ASTPtr& ast, size_t index = 0) {
  const auto* query = getSingleQuery(ast);
  if (!query || index >= query->clauses.size()) return nullptr;

  const auto* stmt = query->clauses[index]->as<GAST::GQLCatalogStatement>();
  EXPECT_NE(stmt, nullptr);
  return stmt;
}

const GAST::GQLMatchClause* getMatchClause(const GAST::GQLSingleQuery& clauses, size_t index = 0) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* match = clauses.clauses[index]->as<GAST::GQLMatchClause>();
  EXPECT_NE(match, nullptr);
  return match;
}

const GAST::GQLSelectClause* getSelectClause(const GAST::GQLSingleQuery& query, size_t index = 0) {
  if (index >= query.clauses.size()) return nullptr;

  const auto* clause = query.clauses[index]->as<GAST::GQLSelectClause>();
  EXPECT_NE(clause, nullptr);
  return clause;
}

const GAST::GQLReturnClause* getReturnClause(const GAST::GQLSingleQuery& clauses, size_t index = 1) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* clause = clauses.clauses[index]->as<GAST::GQLReturnClause>();
  EXPECT_NE(clause, nullptr);
  return clause;
}

const GAST::GQLCallNamedClause* getCallNamedClause(const GAST::GQLSingleQuery& clauses, size_t index = 0) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* call = clauses.clauses[index]->as<GAST::GQLCallNamedClause>();
  EXPECT_NE(call, nullptr);
  return call;
}

const GAST::GQLCallInlineClause* getCallInlineClause(const GAST::GQLSingleQuery& clauses, size_t index = 0) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* call = clauses.clauses[index]->as<GAST::GQLCallInlineClause>();
  EXPECT_NE(call, nullptr);
  return call;
}

const GAST::GQLCallVariableScopeClause* getCallVariableScopeClause(const ASTPtr& ast) {
  const auto* clause = ast->as<GAST::GQLCallVariableScopeClause>();
  EXPECT_NE(clause, nullptr);
  return clause;
}

const GAST::GQLLetClause* getLetClause(const GAST::GQLSingleQuery& clauses, size_t index = 0) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* let_clause = clauses.clauses[index]->as<GAST::GQLLetClause>();
  EXPECT_NE(let_clause, nullptr);
  return let_clause;
}

const GAST::GQLForClause* getForClause(const GAST::GQLSingleQuery& clauses, size_t index = 0) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* for_clause = clauses.clauses[index]->as<GAST::GQLForClause>();
  EXPECT_NE(for_clause, nullptr);
  return for_clause;
}

const GAST::GQLGroupByClause* getGroupByClause(const ASTPtr& ast) {
  const auto* group_by_clause = ast->as<GAST::GQLGroupByClause>();
  EXPECT_NE(group_by_clause, nullptr);
  return group_by_clause;
}

const GAST::GQLFinishClause* getFinishClause(const GAST::GQLSingleQuery& clauses, size_t index) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* finish_clause = clauses.clauses[index]->as<GAST::GQLFinishClause>();
  EXPECT_NE(finish_clause, nullptr);
  return finish_clause;
}

const GAST::GQLAssignmentItem* getAssignmentItem(const ASTPtr& ast) {
  const auto* assignment = ast->as<GAST::GQLAssignmentItem>();
  EXPECT_NE(assignment, nullptr);
  return assignment;
}

const GAST::GQLSelectSourceItem* getSelectSourceItem(const ASTPtr& ast) {
  const auto* source_item = ast->as<GAST::GQLSelectSourceItem>();
  EXPECT_NE(source_item, nullptr);
  return source_item;
}

const GAST::GQLSelectSourceList* getSelectSourceList(const ASTPtr& ast) {
  const auto* source_list = ast->as<GAST::GQLSelectSourceList>();
  EXPECT_NE(source_list, nullptr);
  return source_list;
}

const GAST::GQLPageClause* getPageClause(const GAST::GQLSingleQuery& clauses, size_t index) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* page_clause = clauses.clauses[index]->as<GAST::GQLPageClause>();
  EXPECT_NE(page_clause, nullptr);
  return page_clause;
}

const GAST::GQLUseClause* getUseClause(const GAST::GQLSingleQuery& clauses, size_t index = 0) {
  if (index >= clauses.clauses.size()) return nullptr;

  const auto* use_clause = clauses.clauses[index]->as<GAST::GQLUseClause>();
  EXPECT_NE(use_clause, nullptr);
  return use_clause;
}

const GAST::GQLSubquery* getSubquery(const ASTPtr& ast) {
  const auto* subquery = ast->as<GAST::GQLSubquery>();
  EXPECT_NE(subquery, nullptr);
  return subquery;
}

const GAST::GQLAtSchemaClause* getAtSchemaClause(const ASTPtr& ast) {
  const auto* at_schema_clause = ast->as<GAST::GQLAtSchemaClause>();
  EXPECT_NE(at_schema_clause, nullptr);
  return at_schema_clause;
}

const GAST::GQLBindingVariableDefinitionBlock* getBindingVariableDefinitionBlock(const ASTPtr& ast) {
  const auto* block = ast->as<GAST::GQLBindingVariableDefinitionBlock>();
  EXPECT_NE(block, nullptr);
  return block;
}

const GAST::GQLBindingVariableDefinition* getBindingVariableDefinition(const ASTPtr& ast) {
  const auto* definition = ast->as<GAST::GQLBindingVariableDefinition>();
  EXPECT_NE(definition, nullptr);
  return definition;
}

const GAST::GQLBindingInitializer* getBindingInitializer(const ASTPtr& ast) {
  const auto* initializer = ast->as<GAST::GQLBindingInitializer>();
  EXPECT_NE(initializer, nullptr);
  return initializer;
}

const GAST::GQLGraphExpression* getGraphExpression(const ASTPtr& ast) {
  const auto* expression = ast->as<GAST::GQLGraphExpression>();
  EXPECT_NE(expression, nullptr);
  return expression;
}

const GAST::GQLBindingTableExpression* getBindingTableExpression(const ASTPtr& ast) {
  const auto* expression = ast->as<GAST::GQLBindingTableExpression>();
  EXPECT_NE(expression, nullptr);
  return expression;
}

const GAST::GQLSubqueryNextClause* getSubqueryNextClause(const ASTPtr& ast) {
  const auto* next_clause = ast->as<GAST::GQLSubqueryNextClause>();
  EXPECT_NE(next_clause, nullptr);
  return next_clause;
}

const GAST::GQLYieldClause* getYieldClause(const ASTPtr& ast) {
  const auto* yield_clause = ast->as<GAST::GQLYieldClause>();
  EXPECT_NE(yield_clause, nullptr);
  return yield_clause;
}

const GAST::GQLSchemaReference* getSchemaReference(const ASTPtr& ast) {
  const auto* schema_reference = ast->as<GAST::GQLSchemaReference>();
  EXPECT_NE(schema_reference, nullptr);
  return schema_reference;
}

const GAST::GQLPathPattern* getOnlyPathPattern(const GAST::GQLMatchClause& match) {
  if (match.path_patterns.size() != 1) return nullptr;

  const auto* path = match.path_patterns.front()->as<GAST::GQLPathPattern>();
  EXPECT_NE(path, nullptr);
  return path;
}

const GAST::GQLPathPattern* getPathPattern(const ASTPtr& ast) {
  const auto* path = ast->as<GAST::GQLPathPattern>();
  EXPECT_NE(path, nullptr);
  return path;
}

const GAST::GQLPathTerm* getPathTerm(const ASTPtr& ast) {
  const auto* term = ast->as<GAST::GQLPathTerm>();
  EXPECT_NE(term, nullptr);
  return term;
}

const GAST::GQLPathTerm* getPathTerm(const GAST::GQLPathPattern& path) { return getPathTerm(path.expression); }

const GAST::GQLPathPatternAlternation* getPathPatternAlternation(const ASTPtr& ast) {
  const auto* alternation = ast->as<GAST::GQLPathPatternAlternation>();
  EXPECT_NE(alternation, nullptr);
  return alternation;
}

const GAST::GQLQuantifiedPathPrimary* getQuantifiedPathPrimary(const ASTPtr& ast) {
  const auto* quantified = ast->as<GAST::GQLQuantifiedPathPrimary>();
  EXPECT_NE(quantified, nullptr);
  return quantified;
}

const GAST::GQLPathModePrefix* getPathModePrefix(const ASTPtr& ast) {
  const auto* prefix = ast->as<GAST::GQLPathModePrefix>();
  EXPECT_NE(prefix, nullptr);
  return prefix;
}

const GAST::GQLPathSearchPrefix* getPathSearchPrefix(const ASTPtr& ast) {
  const auto* prefix = ast->as<GAST::GQLPathSearchPrefix>();
  EXPECT_NE(prefix, nullptr);
  return prefix;
}

const GAST::GQLCountSpec* getCountSpec(const ASTPtr& ast) {
  const auto* count = ast->as<GAST::GQLCountSpec>();
  EXPECT_NE(count, nullptr);
  return count;
}

const GAST::GQLKeepClause* getKeepClause(const ASTPtr& ast) {
  const auto* clause = ast->as<GAST::GQLKeepClause>();
  EXPECT_NE(clause, nullptr);
  return clause;
}

const GAST::GQLParenthesizedPathPattern* getParenthesizedPathPattern(const ASTPtr& ast) {
  const auto* path = ast->as<GAST::GQLParenthesizedPathPattern>();
  EXPECT_NE(path, nullptr);
  return path;
}

const GAST::GQLSimplifiedPathPattern* getSimplifiedPathPattern(const ASTPtr& ast) {
  const auto* path = ast->as<GAST::GQLSimplifiedPathPattern>();
  EXPECT_NE(path, nullptr);
  return path;
}

const GAST::GQLSimplifiedPathExpr* getSimplifiedPathExpr(const ASTPtr& ast) {
  const auto* expression = ast->as<GAST::GQLSimplifiedPathExpr>();
  EXPECT_NE(expression, nullptr);
  return expression;
}

const GAST::GQLMatchStatementBlock* getMatchStatementBlock(const ASTPtr& ast) {
  const auto* block = ast->as<GAST::GQLMatchStatementBlock>();
  EXPECT_NE(block, nullptr);
  return block;
}

const GAST::GQLGraphPatternBlock* getGraphPatternBlock(const ASTPtr& ast) {
  const auto* block = ast->as<GAST::GQLGraphPatternBlock>();
  EXPECT_NE(block, nullptr);
  return block;
}

const GAST::GQLAliasedItem* getAliasedItem(const ASTPtr& ast) {
  const auto* item = ast->as<GAST::GQLAliasedItem>();
  EXPECT_NE(item, nullptr);
  return item;
}

const ASTPtr& unwrapAliasedItemExpression(const ASTPtr& ast) {
  if (const auto* item = ast->as<GAST::GQLAliasedItem>()) {
    EXPECT_NE(item->expression, nullptr);
    return item->expression;
  }

  return ast;
}

const GAST::GQLListConstructor* getListConstructor(const ASTPtr& ast) {
  const auto* constructor = unwrapAliasedItemExpression(ast)->as<GAST::GQLListConstructor>();
  EXPECT_NE(constructor, nullptr);
  return constructor;
}

const GAST::GQLRecordConstructor* getRecordConstructor(const ASTPtr& ast) {
  const auto* constructor = unwrapAliasedItemExpression(ast)->as<GAST::GQLRecordConstructor>();
  EXPECT_NE(constructor, nullptr);
  return constructor;
}

const GAST::GQLExpr* getExpr(const ASTPtr& ast) {
  const auto* expression = unwrapAliasedItemExpression(ast)->as<GAST::GQLExpr>();
  EXPECT_NE(expression, nullptr);
  return expression;
}

const GAST::GQLCaseExpr* getCaseExpr(const ASTPtr& ast) {
  const auto* expression = unwrapAliasedItemExpression(ast)->as<GAST::GQLCaseExpr>();
  EXPECT_NE(expression, nullptr);
  return expression;
}

const GAST::GQLLabelExpression* getLabelExpr(const ASTPtr& ast) {
  const auto* expression = ast->as<GAST::GQLLabelExpression>();
  EXPECT_NE(expression, nullptr);
  return expression;
}

const GAST::GQLQuantifier* getQuantifier(const ASTPtr& ast) {
  const auto* quantifier = ast->as<GAST::GQLQuantifier>();
  EXPECT_NE(quantifier, nullptr);
  return quantifier;
}

void attachChildIfPresent(IAST& owner, const ASTPtr& child) {
  if (child) owner.children.push_back(child);
}

void assertNormalizedRoundTrip(std::string_view query)
{
  SCOPED_TRACE(query);
  auto ast1 = parseGraphOrThrow(query);
  ASSERT_NE(ast1, nullptr);
  auto first = formatAST(*ast1);
  ASSERT_FALSE(first.empty());

  auto ast2 = parseGraphOrThrow(first);
  ASSERT_NE(ast2, nullptr) << "re-parse failed for: " << first;
  auto second = formatAST(*ast2);
  EXPECT_EQ(second, first);
}

void collectASTNodePointers(const IAST& ast, std::unordered_set<const IAST*>& pointers) {
  pointers.insert(&ast);

  for (const auto& child : ast.children) {
    ASSERT_NE(child, nullptr);
    collectASTNodePointers(*child, pointers);
  }
}

void assertCloneHasNoSharedNodes(const IAST& ast, const std::unordered_set<const IAST*>& source_pointers) {
  EXPECT_EQ(source_pointers.find(&ast), source_pointers.end()) << ast.getID();

  for (const auto& child : ast.children) {
    ASSERT_NE(child, nullptr);
    assertCloneHasNoSharedNodes(*child, source_pointers);
  }
}

void assertASTContract(const ASTPtr& ast) {
  ASSERT_NE(ast, nullptr);

  std::unordered_set<const IAST*> source_pointers;
  collectASTNodePointers(*ast, source_pointers);

  auto cloned = ast->clone();
  ASSERT_NE(cloned, nullptr);
  EXPECT_NE(cloned.get(), ast.get());
  EXPECT_EQ(formatAST(*cloned), formatAST(*ast));
  assertCloneHasNoSharedNodes(*cloned, source_pointers);

  auto reparsed = parseDMLOrThrow(formatAST(*ast));
  ASSERT_NE(reparsed, nullptr);
  EXPECT_EQ(formatAST(*reparsed), formatAST(*ast));
}

}  // namespace

TEST(GQLParser, SimpleMatchClause) {
  auto ast = parseGraphOrThrow("MATCH (n:Person) RETURN n");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  EXPECT_FALSE(match->optional);

  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* node = term->factors[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(node, nullptr);

  const auto* variable = getExpr(node->variable);
  ASSERT_NE(variable, nullptr);
  EXPECT_EQ(variable->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(variable->text, "n");

  const auto* label = getLabelExpr(node->label_expression);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->kind, GAST::GQLLabelExpression::Kind::Name);
  EXPECT_EQ(label->text, "Person");
}

TEST(GQLParser, FormatParseFormatCorpus) {
  const std::vector<std::string_view> queries = {
      "RETURN 1 + x",
      "RETURN ABS(x)",
      "MATCH (n) RETURN n.name",
      "LET VALUE y INT = 2 RETURN y",
      "RETURN CAST(1 AS INT)",
      "MATCH (n) WHERE n IS TYPED STRING RETURN n",
      "CREATE GRAPH TYPE gt AS { (:Person {name STRING}) }",
  };

  for (auto query : queries) assertNormalizedRoundTrip(query);
}

TEST(GQLParser, OptionalMatchRequiresGQLDialect) {
  EXPECT_THROW((void)parseTopLevelOrThrow("OPTIONAL MATCH (a) RETURN a"), DB::Exception);
}

TEST(GQLParser, GraphSelectRequiresGQLDialect) {
  EXPECT_THROW((void)parseTopLevelOrThrow("SELECT a FROM { MATCH (a) RETURN a }"), DB::Exception);
}

TEST(GQLParser, FocusedUseQueryRequiresGQLDialect) {
  EXPECT_THROW((void)parseTopLevelOrThrow("USE foo MATCH (a) RETURN a"), DB::Exception);
}

TEST(GQLParser, CallRequiresGQLDialect) {
  EXPECT_THROW((void)parseTopLevelOrThrow("CALL foo(a) YIELD x RETURN x"), DB::Exception);
}

TEST(GQLParser, PlainSqlSelectFallsBackToSqlParser) {
  auto ast = parseTopLevelOrThrow("SELECT 1");
  ASSERT_EQ(ast->as<GAST::GQLSingleQuery>(), nullptr);
}

TEST(GQLParser, TopLevelParserKeepsClickHouseSettings) {
  auto ast = parseTopLevelOrThrow("SET max_threads = 1");
  ASSERT_NE(ast, nullptr);
  ASSERT_NE(ast->as<ASTSetQuery>(), nullptr);
  ASSERT_EQ(ast->as<GAST::GQLSingleQuery>(), nullptr);
}

TEST(GQLParser, PlainSqlUseFallsBackToSqlParser) {
  auto ast = parseTopLevelOrThrow("USE default");
  ASSERT_EQ(ast->as<GAST::GQLSingleQuery>(), nullptr);
}

TEST(GQLParser, FocusedUseGraphQueryPreservesUseClause) {
  auto ast = parseGraphOrThrow("USE foo MATCH (a) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);

  const auto* use_clause = getUseClause(*clauses, 0);
  ASSERT_NE(use_clause, nullptr);
  const auto* graph_reference = getGraphExpression(use_clause->graph_reference);
  ASSERT_NE(graph_reference, nullptr);
  EXPECT_EQ(graph_reference->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(graph_reference->text, "foo");
  EXPECT_EQ(formatAST(*use_clause), "USE foo");

  const auto* match = getMatchClause(*clauses, 1);
  const auto* project = getReturnClause(*clauses, 2);
  ASSERT_NE(match, nullptr);
  ASSERT_NE(project, nullptr);
}

TEST(GQLParser, FocusedNestedQueryKeepsSubqueryWrapper) {
  auto ast = parseGraphOrThrow("USE foo { MATCH (a) RETURN a }");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* use_clause = getUseClause(*clauses, 0);
  ASSERT_NE(use_clause, nullptr);
  const auto* graph_reference = getGraphExpression(use_clause->graph_reference);
  ASSERT_NE(graph_reference, nullptr);
  EXPECT_EQ(graph_reference->text, "foo");
  EXPECT_EQ(formatAST(*use_clause), "USE foo");

  const auto* subquery = getSubquery(clauses->clauses[1]);
  ASSERT_NE(subquery, nullptr);
  ASSERT_NE(getClausesQuery(subquery->query), nullptr);
  EXPECT_EQ(formatAST(*subquery), "{ MATCH (a) RETURN a }");
}

TEST(GQLParser, FocusedUsePrimitiveReturn) {
  auto ast = parseGraphOrThrow("USE foo RETURN 1");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);
  ASSERT_NE(getUseClause(*clauses, 0), nullptr);
  EXPECT_EQ(getGraphExpression(getUseClause(*clauses, 0)->graph_reference)->text, "foo");
  ASSERT_NE(getReturnClause(*clauses), nullptr);
  EXPECT_EQ(formatAST(*clauses), "USE foo RETURN 1");
  assertNormalizedRoundTrip("USE foo RETURN 1");
}

TEST(GQLParser, FocusedUseFinish) {
  auto ast = parseGraphOrThrow("USE foo FINISH");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);
  ASSERT_NE(getUseClause(*clauses, 0), nullptr);
  EXPECT_EQ(getGraphExpression(getUseClause(*clauses, 0)->graph_reference)->text, "foo");
  ASSERT_NE(getFinishClause(*clauses, 1), nullptr);
  EXPECT_EQ(formatAST(*clauses), "USE foo FINISH");
  assertNormalizedRoundTrip("USE foo FINISH");
}

TEST(GQLParser, FocusedUseMultiPartClauseOrder) {
  auto ast = parseGraphOrThrow("USE foo MATCH (a) USE bar MATCH (b) RETURN a, b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 5);
  ASSERT_NE(getUseClause(*clauses, 0), nullptr);
  EXPECT_EQ(getGraphExpression(getUseClause(*clauses, 0)->graph_reference)->text, "foo");
  ASSERT_NE(getMatchClause(*clauses, 1), nullptr);
  ASSERT_NE(getUseClause(*clauses, 2), nullptr);
  EXPECT_EQ(getGraphExpression(getUseClause(*clauses, 2)->graph_reference)->text, "bar");
  ASSERT_NE(getMatchClause(*clauses, 3), nullptr);
  ASSERT_NE(getReturnClause(*clauses, 4), nullptr);
  assertNormalizedRoundTrip("USE foo MATCH (a) USE bar MATCH (b) RETURN a, b");
}

TEST(GQLParser, FilterSearchConditionBuildsWhereClause) {
  auto ast = parseGraphOrThrow("MATCH (a) FILTER a.age > 30 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);
  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);
  const auto* filter = clauses->clauses[1]->as<GAST::GQLWhereClause>();
  ASSERT_NE(filter, nullptr);
  EXPECT_EQ(filter->type, GAST::GQLWhereClause::Type::Filter);
  ASSERT_NE(getExpr(filter->expression), nullptr);
  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) FILTER (a.age > 30) RETURN a");
  assertNormalizedRoundTrip("MATCH (a) FILTER (a.age > 30) RETURN a");
}

TEST(GQLParser, FilterWhereClausePreservesFilterType) {
  auto ast = parseGraphOrThrow("MATCH (a) FILTER WHERE a.age > 30 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);
  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);
  const auto* filter = clauses->clauses[1]->as<GAST::GQLWhereClause>();
  ASSERT_NE(filter, nullptr);
  EXPECT_EQ(filter->type, GAST::GQLWhereClause::Type::Filter);
  ASSERT_NE(getExpr(filter->expression), nullptr);
  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) FILTER (a.age > 30) RETURN a");
  assertNormalizedRoundTrip("MATCH (a) FILTER (a.age > 30) RETURN a");
}

TEST(GQLParser, FocusedUseWithFilter) {
  auto ast = parseGraphOrThrow("USE foo MATCH (a) FILTER a IS NOT NULL RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 4);
  ASSERT_NE(getUseClause(*clauses, 0), nullptr);
  ASSERT_NE(getMatchClause(*clauses, 1), nullptr);
  const auto* filter = clauses->clauses[2]->as<GAST::GQLWhereClause>();
  ASSERT_NE(filter, nullptr);
  EXPECT_EQ(filter->type, GAST::GQLWhereClause::Type::Filter);
  ASSERT_NE(getReturnClause(*clauses, 3), nullptr);
  assertNormalizedRoundTrip("USE foo MATCH (a) FILTER (a IS NOT NULL) RETURN a");
}

TEST(GQLParser, FilterExistsSubquery) {
  auto ast = parseGraphOrThrow("MATCH (a) FILTER EXISTS { MATCH (a)-[e]->(b) } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);
  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);
  const auto* filter = clauses->clauses[1]->as<GAST::GQLWhereClause>();
  ASSERT_NE(filter, nullptr);
  EXPECT_EQ(filter->type, GAST::GQLWhereClause::Type::Filter);
  const auto* exists = getExpr(filter->expression);
  ASSERT_NE(exists, nullptr);
  EXPECT_EQ(exists->kind, GAST::GQLExpr::Kind::UnaryOp);
  EXPECT_EQ(exists->text, "EXISTS ");
  ASSERT_EQ(exists->children.size(), 1);
  ASSERT_NE(getMatchStatementBlock(exists->children[0]), nullptr);
  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { MATCH (a)-[e]->(b) } RETURN a");
}

TEST(GQLParser, SelectStatementBuildsStructuredSingleQuery) {
  auto ast = parseGraphOrThrow(
      "SELECT DISTINCT a AS x FROM { MATCH (a) RETURN a } WHERE a IS NOT NULL GROUP BY a HAVING a IS NOT NULL ORDER BY a DESC OFFSET 1 "
      "LIMIT 2");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* select_clause = getSelectClause(*clauses, 0);
  ASSERT_NE(select_clause, nullptr);
  EXPECT_TRUE(select_clause->distinct);
  ASSERT_EQ(select_clause->items.size(), 1);

  const auto* select_item = getAliasedItem(select_clause->items[0]);
  ASSERT_NE(select_item, nullptr);
  EXPECT_EQ(select_item->alias, "x");

  const auto* item = getExpr(select_item->expression);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->text, "a");

  const auto* source = getSubquery(select_clause->source);
  ASSERT_NE(source, nullptr);
  ASSERT_NE(getClausesQuery(source->query), nullptr);

  const auto* where = select_clause->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* having = select_clause->having->as<GAST::GQLWhereClause>();
  ASSERT_NE(having, nullptr);
  const auto* group_by = getGroupByClause(select_clause->group_by);
  ASSERT_NE(group_by, nullptr);
  ASSERT_EQ(group_by->items.size(), 1);
  EXPECT_EQ(getExpr(group_by->items[0])->text, "a");

  const auto* page_clause = getPageClause(*clauses, 1);
  ASSERT_NE(page_clause, nullptr);
  const auto* order_by = page_clause->order_by->as<GAST::GQLOrderByClause>();
  ASSERT_NE(order_by, nullptr);
  const auto* offset = getExpr(page_clause->offset);
  const auto* limit = getExpr(page_clause->limit);
  ASSERT_NE(offset, nullptr);
  ASSERT_NE(limit, nullptr);
  EXPECT_EQ(offset->text, "1");
  EXPECT_EQ(limit->text, "2");
  EXPECT_EQ(formatAST(*clauses),
            "SELECT DISTINCT a AS x FROM { MATCH (a) RETURN a } WHERE (a IS NOT NULL) GROUP BY a HAVING (a IS NOT NULL) ORDER BY a DESC "
            "OFFSET 1 LIMIT 2");
}

TEST(GQLParser, SelectStatementPreservesGraphQualifiedQuerySource) {
  auto ast = parseGraphOrThrow("SELECT a FROM foo { MATCH (a) RETURN a }");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* select_clause = getSelectClause(*clauses, 0);
  ASSERT_NE(select_clause, nullptr);

  const auto* source_item = getSelectSourceItem(select_clause->source);
  ASSERT_NE(source_item, nullptr);

  const auto* graph_reference = getGraphExpression(source_item->graph_reference);
  ASSERT_NE(graph_reference, nullptr);
  EXPECT_EQ(graph_reference->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(graph_reference->text, "foo");

  const auto* nested_query = getSubquery(source_item->source);
  ASSERT_NE(nested_query, nullptr);
  ASSERT_NE(getClausesQuery(nested_query->query), nullptr);
  EXPECT_EQ(formatAST(*clauses), "SELECT a FROM foo { MATCH (a) RETURN a }");
}

TEST(GQLParser, NestedQueryKeepsNextStatementsInsideSubqueryClause) {
  auto ast = parseGraphOrThrow("USE foo { MATCH (a) RETURN a NEXT YIELD a RETURN a }");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* subquery = getSubquery(clauses->clauses[1]);
  ASSERT_NE(subquery, nullptr);
  ASSERT_EQ(subquery->next_statements.size(), 1);

  const auto* next_clause = getSubqueryNextClause(subquery->next_statements[0]);
  ASSERT_NE(next_clause, nullptr);
  const auto* yield_clause = getYieldClause(next_clause->yield);
  ASSERT_NE(yield_clause, nullptr);
  ASSERT_EQ(yield_clause->items.size(), 1);
  EXPECT_EQ(getExpr(yield_clause->items[0])->text, "a");
  ASSERT_NE(getClausesQuery(next_clause->query), nullptr);
  EXPECT_EQ(formatAST(*subquery), "{ MATCH (a) RETURN a NEXT YIELD a RETURN a }");
}

TEST(GQLParser, NestedQueryCloneKeepsChildrenOrder) {
  auto ast = parseGraphOrThrow("{ AT foo VALUE x = 1 MATCH (a) RETURN a NEXT YIELD a RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);
  ASSERT_EQ(subquery->children.size(), 4);
  ASSERT_NE(subquery->children[0]->as<GAST::GQLAtSchemaClause>(), nullptr);
  ASSERT_NE(subquery->children[1]->as<GAST::GQLBindingVariableDefinitionBlock>(), nullptr);
  ASSERT_NE(subquery->children[2]->as<GAST::GQLSingleQuery>(), nullptr);
  ASSERT_NE(subquery->children[3]->as<GAST::GQLSubqueryNextClause>(), nullptr);

  auto cloned = subquery->clone();
  const auto* cloned_subquery = getSubquery(cloned);
  ASSERT_NE(cloned_subquery, nullptr);
  ASSERT_EQ(cloned_subquery->children.size(), 4);
  EXPECT_NE(cloned_subquery->children[0]->as<GAST::GQLAtSchemaClause>(), nullptr);
  EXPECT_NE(cloned_subquery->children[1]->as<GAST::GQLBindingVariableDefinitionBlock>(), nullptr);
  EXPECT_NE(cloned_subquery->children[2]->as<GAST::GQLSingleQuery>(), nullptr);
  EXPECT_NE(cloned_subquery->children[3]->as<GAST::GQLSubqueryNextClause>(), nullptr);
}

TEST(GQLParser, NestedQueryPreservesStructuredSchemaAndBindings) {
  auto ast = parseGraphOrThrow("{ AT ../foo/bar/analytics VALUE x = 1 MATCH (a) RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);

  const auto* at_schema_clause = getAtSchemaClause(subquery->at_schema);
  ASSERT_NE(at_schema_clause, nullptr);
  const auto* schema_reference = getSchemaReference(at_schema_clause->schema_reference);
  ASSERT_NE(schema_reference, nullptr);
  EXPECT_EQ(schema_reference->kind, GAST::GQLSchemaReference::Kind::RelativePath);
  EXPECT_EQ(schema_reference->parent_levels, 1);
  ASSERT_EQ(schema_reference->directory_parts.size(), 2);
  EXPECT_EQ(schema_reference->directory_parts[0], "foo");
  EXPECT_EQ(schema_reference->directory_parts[1], "bar");
  EXPECT_EQ(schema_reference->name, "analytics");

  const auto* binding_block = getBindingVariableDefinitionBlock(subquery->bindings);
  ASSERT_NE(binding_block, nullptr);
  ASSERT_EQ(binding_block->definitions.size(), 1);

  const auto* binding_definition = getBindingVariableDefinition(binding_block->definitions[0]);
  ASSERT_NE(binding_definition, nullptr);
  EXPECT_EQ(binding_definition->kind, GAST::GQLBindingVariableDefinition::Kind::Value);
  EXPECT_EQ(binding_definition->name, "x");
  EXPECT_EQ(binding_definition->type, nullptr);
  const auto* initializer = getBindingInitializer(binding_definition->initializer);
  ASSERT_NE(initializer, nullptr);
  EXPECT_EQ(initializer->kind, GAST::GQLBindingInitializer::Kind::Value);
  EXPECT_EQ(getExpr(initializer->value)->text, "1");

  ASSERT_NE(getClausesQuery(subquery->query), nullptr);
  EXPECT_EQ(formatAST(*subquery), "{ AT ../foo/bar/analytics VALUE x = 1 MATCH (a) RETURN a }");
}

TEST(GQLParser, NestedQueryPreservesStructuredBindingTableInitializer) {
  auto ast = parseGraphOrThrow("{ TABLE t = { MATCH (a) RETURN a } MATCH (a) RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);

  const auto* binding_block = getBindingVariableDefinitionBlock(subquery->bindings);
  ASSERT_NE(binding_block, nullptr);
  ASSERT_EQ(binding_block->definitions.size(), 1);

  const auto* binding_definition = getBindingVariableDefinition(binding_block->definitions[0]);
  ASSERT_NE(binding_definition, nullptr);
  EXPECT_EQ(binding_definition->kind, GAST::GQLBindingVariableDefinition::Kind::BindingTable);
  EXPECT_EQ(binding_definition->name, "t");

  const auto* initializer = getBindingInitializer(binding_definition->initializer);
  ASSERT_NE(initializer, nullptr);
  EXPECT_EQ(initializer->kind, GAST::GQLBindingInitializer::Kind::BindingTable);

  const auto* binding_table_expression = getBindingTableExpression(initializer->value);
  ASSERT_NE(binding_table_expression, nullptr);
  EXPECT_EQ(binding_table_expression->kind, GAST::GQLBindingTableExpression::Kind::NestedQuery);

  const auto* nested_binding_query = getSubquery(binding_table_expression->value);
  ASSERT_NE(nested_binding_query, nullptr);
  ASSERT_NE(getClausesQuery(nested_binding_query->query), nullptr);
  EXPECT_EQ(formatAST(*subquery), "{ TABLE t = { MATCH (a) RETURN a } MATCH (a) RETURN a }");
}

TEST(GQLParser, NestedQueryPreservesStructuredGraphInitializer) {
  auto ast = parseGraphOrThrow("{ GRAPH g = CURRENT_GRAPH MATCH (a) RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);

  const auto* binding_block = getBindingVariableDefinitionBlock(subquery->bindings);
  ASSERT_NE(binding_block, nullptr);
  ASSERT_EQ(binding_block->definitions.size(), 1);

  const auto* binding_definition = getBindingVariableDefinition(binding_block->definitions[0]);
  ASSERT_NE(binding_definition, nullptr);
  EXPECT_EQ(binding_definition->kind, GAST::GQLBindingVariableDefinition::Kind::Graph);
  EXPECT_EQ(binding_definition->name, "g");

  const auto* initializer = getBindingInitializer(binding_definition->initializer);
  ASSERT_NE(initializer, nullptr);
  EXPECT_EQ(initializer->kind, GAST::GQLBindingInitializer::Kind::Graph);

  const auto* graph_expression = getGraphExpression(initializer->value);
  ASSERT_NE(graph_expression, nullptr);
  EXPECT_EQ(graph_expression->kind, GAST::GQLGraphExpression::Kind::CurrentGraph);
  EXPECT_EQ(graph_expression->text, "CURRENT_GRAPH");
  EXPECT_EQ(formatAST(*subquery), "{ GRAPH g = CURRENT_GRAPH MATCH (a) RETURN a }");
}

TEST(GQLParser, NestedQueryPreservesStructuredBindingTableReferenceInitializer) {
  auto ast = parseGraphOrThrow("{ TABLE t = foo MATCH (a) RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);

  const auto* binding_block = getBindingVariableDefinitionBlock(subquery->bindings);
  ASSERT_NE(binding_block, nullptr);
  ASSERT_EQ(binding_block->definitions.size(), 1);

  const auto* binding_definition = getBindingVariableDefinition(binding_block->definitions[0]);
  ASSERT_NE(binding_definition, nullptr);
  EXPECT_EQ(binding_definition->kind, GAST::GQLBindingVariableDefinition::Kind::BindingTable);
  EXPECT_EQ(binding_definition->name, "t");

  const auto* initializer = getBindingInitializer(binding_definition->initializer);
  ASSERT_NE(initializer, nullptr);
  EXPECT_EQ(initializer->kind, GAST::GQLBindingInitializer::Kind::BindingTable);

  const auto* binding_table_expression = getBindingTableExpression(initializer->value);
  ASSERT_NE(binding_table_expression, nullptr);
  EXPECT_EQ(binding_table_expression->kind, GAST::GQLBindingTableExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(binding_table_expression->text, "foo");
  EXPECT_EQ(formatAST(*subquery), "{ TABLE t = foo MATCH (a) RETURN a }");
}

TEST(GQLParser, GraphExpressionObjectExpressionVariable) {
  auto ast = parseGraphOrThrow("USE VARIABLE x MATCH (a) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* use_clause = getUseClause(*clauses, 0);
  ASSERT_NE(use_clause, nullptr);

  const auto* graph_ref = getGraphExpression(use_clause->graph_reference);
  ASSERT_NE(graph_ref, nullptr);
  EXPECT_EQ(graph_ref->kind, GAST::GQLGraphExpression::Kind::ObjectExpression);
  ASSERT_NE(graph_ref->value, nullptr);

  const auto* inner = getExpr(graph_ref->value);
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->kind, GAST::GQLExpr::Kind::VariableExpression);
  EXPECT_EQ(inner->text, "VARIABLE");
  ASSERT_EQ(inner->children.size(), 1);

  const auto* operand = getExpr(inner->children[0]);
  ASSERT_NE(operand, nullptr);
  EXPECT_EQ(operand->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(operand->text, "x");
}

TEST(GQLParser, GraphExpressionObjectExpressionParenthesized) {
  auto ast = parseGraphOrThrow("USE (x) MATCH (a) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* use_clause = getUseClause(*clauses, 0);
  ASSERT_NE(use_clause, nullptr);

  const auto* graph_ref = getGraphExpression(use_clause->graph_reference);
  ASSERT_NE(graph_ref, nullptr);
  EXPECT_EQ(graph_ref->kind, GAST::GQLGraphExpression::Kind::ObjectExpression);
  ASSERT_NE(graph_ref->value, nullptr);

  const auto* inner = getExpr(graph_ref->value);
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(inner->text, "x");
}

TEST(GQLParser, GraphExpressionObjectExpressionSpecialCase) {
  auto ast = parseGraphOrThrow("USE ELEMENT_ID(n) MATCH (a) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* use_clause = getUseClause(*clauses, 0);
  ASSERT_NE(use_clause, nullptr);

  const auto* graph_ref = getGraphExpression(use_clause->graph_reference);
  ASSERT_NE(graph_ref, nullptr);
  EXPECT_EQ(graph_ref->kind, GAST::GQLGraphExpression::Kind::ObjectExpression);
  ASSERT_NE(graph_ref->value, nullptr);

  const auto* inner = getExpr(graph_ref->value);
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(inner->text, "ELEMENT_ID");
  ASSERT_EQ(inner->children.size(), 1);

  const auto* arg = getExpr(inner->children[0]);
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(arg->text, "n");
}

TEST(GQLParser, BindingTableExpressionObjectExpressionVariable) {
  auto ast = parseGraphOrThrow("{ TABLE t = VARIABLE y MATCH (a) RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);

  const auto* binding_block = getBindingVariableDefinitionBlock(subquery->bindings);
  ASSERT_NE(binding_block, nullptr);
  ASSERT_EQ(binding_block->definitions.size(), 1);

  const auto* binding_def = getBindingVariableDefinition(binding_block->definitions[0]);
  ASSERT_NE(binding_def, nullptr);

  const auto* initializer = getBindingInitializer(binding_def->initializer);
  ASSERT_NE(initializer, nullptr);
  EXPECT_EQ(initializer->kind, GAST::GQLBindingInitializer::Kind::BindingTable);

  const auto* bt_expr = getBindingTableExpression(initializer->value);
  ASSERT_NE(bt_expr, nullptr);
  EXPECT_EQ(bt_expr->kind, GAST::GQLBindingTableExpression::Kind::ObjectExpression);
  ASSERT_NE(bt_expr->value, nullptr);

  const auto* inner = getExpr(bt_expr->value);
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->kind, GAST::GQLExpr::Kind::VariableExpression);
  EXPECT_EQ(inner->text, "VARIABLE");
  ASSERT_EQ(inner->children.size(), 1);

  const auto* operand = getExpr(inner->children[0]);
  ASSERT_NE(operand, nullptr);
  EXPECT_EQ(operand->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(operand->text, "y");
}

TEST(GQLParser, ValueExpressionGraphExpression) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN GRAPH g");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::GraphExpression);
  EXPECT_EQ(expr->text, "GRAPH");
  ASSERT_EQ(expr->children.size(), 1);

  const auto* graph_child = expr->children[0]->as<GAST::GQLGraphExpression>();
  ASSERT_NE(graph_child, nullptr);
  EXPECT_EQ(graph_child->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(graph_child->text, "g");
}

TEST(GQLParser, ValueExpressionPropertyGraphExpression) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN PROPERTY GRAPH g");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::GraphExpression);
  EXPECT_EQ(expr->text, "PROPERTY GRAPH");
  ASSERT_EQ(expr->children.size(), 1);

  const auto* graph_child = expr->children[0]->as<GAST::GQLGraphExpression>();
  ASSERT_NE(graph_child, nullptr);
  EXPECT_EQ(graph_child->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(graph_child->text, "g");
}

TEST(GQLParser, ValueExpressionBindingTableExpression) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TABLE t");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::BindingTableExpression);
  EXPECT_EQ(expr->text, "TABLE");
  ASSERT_EQ(expr->children.size(), 1);

  const auto* table_child = expr->children[0]->as<GAST::GQLBindingTableExpression>();
  ASSERT_NE(table_child, nullptr);
  EXPECT_EQ(table_child->kind, GAST::GQLBindingTableExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(table_child->text, "t");
}

TEST(GQLParser, ValueExpressionBindingTableExpressionWithBinding) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN BINDING TABLE t");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::BindingTableExpression);
  EXPECT_EQ(expr->text, "BINDING TABLE");
  ASSERT_EQ(expr->children.size(), 1);

  const auto* table_child = expr->children[0]->as<GAST::GQLBindingTableExpression>();
  ASSERT_NE(table_child, nullptr);
  EXPECT_EQ(table_child->kind, GAST::GQLBindingTableExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(table_child->text, "t");
}

TEST(GQLParser, NestedCallAcceptsCatalogModifyingStatement) {
  auto ast = parseGraphOrThrow("CALL { DROP GRAPH g }");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 1);
}

TEST(GQLParser, SelectStatementBuildsStructuredGraphMatchSourceList) {
  auto ast = parseGraphOrThrow("SELECT a, b FROM foo MATCH (a), bar MATCH (b) WHERE a = b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* select_clause = getSelectClause(*clauses, 0);
  ASSERT_NE(select_clause, nullptr);

  const auto* source_list = getSelectSourceList(select_clause->source);
  ASSERT_NE(source_list, nullptr);
  ASSERT_EQ(source_list->items.size(), 2);

  const auto* first_item = getSelectSourceItem(source_list->items[0]);
  ASSERT_NE(first_item, nullptr);
  const auto* first_graph = getGraphExpression(first_item->graph_reference);
  ASSERT_NE(first_graph, nullptr);
  EXPECT_EQ(first_graph->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(first_graph->text, "foo");
  ASSERT_NE(first_item->source->as<GAST::GQLMatchClause>(), nullptr);

  const auto* second_item = getSelectSourceItem(source_list->items[1]);
  ASSERT_NE(second_item, nullptr);
  const auto* second_graph = getGraphExpression(second_item->graph_reference);
  ASSERT_NE(second_graph, nullptr);
  EXPECT_EQ(second_graph->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(second_graph->text, "bar");
  ASSERT_NE(second_item->source->as<GAST::GQLMatchClause>(), nullptr);

  EXPECT_EQ(formatAST(*clauses), "SELECT a, b FROM foo MATCH (a), bar MATCH (b) WHERE (a = b)");
}

TEST(GQLParser, NamedCallClauseBuildsStructuredNode) {
  auto ast = parseGraphOrThrow("CALL foo(a, b) YIELD x AS y RETURN y");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* call = getCallNamedClause(*clauses, 0);
  ASSERT_NE(call, nullptr);
  EXPECT_FALSE(call->optional);

  const auto* procedure = call->procedure->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(procedure, nullptr);
  EXPECT_EQ(procedure->name, "foo");
  EXPECT_TRUE(procedure->parent_parts.empty());

  ASSERT_EQ(call->arguments.size(), 2);
  EXPECT_EQ(getExpr(call->arguments[0])->text, "a");
  EXPECT_EQ(getExpr(call->arguments[1])->text, "b");

  const auto* yield_clause = getYieldClause(call->yield);
  ASSERT_NE(yield_clause, nullptr);
  ASSERT_EQ(yield_clause->items.size(), 1);
  const auto* aliased_yield_item = getAliasedItem(yield_clause->items[0]);
  ASSERT_NE(aliased_yield_item, nullptr);
  EXPECT_EQ(aliased_yield_item->alias, "y");

  const auto* yield_item = getExpr(aliased_yield_item->expression);
  ASSERT_NE(yield_item, nullptr);
  EXPECT_EQ(yield_item->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(yield_item->text, "x");
  EXPECT_EQ(formatAST(*call), "CALL foo(a, b) YIELD x AS y");
}

TEST(GQLParser, NamedCallPreservesQualifiedProcedureReference) {
  auto ast = parseGraphOrThrow("CALL foo.bar() YIELD x");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* call = getCallNamedClause(*clauses, 0);
  ASSERT_NE(call, nullptr);
  const auto* procedure = call->procedure->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(procedure, nullptr);
  EXPECT_EQ(procedure->name, "bar");
  ASSERT_EQ(procedure->parent_parts.size(), 1);
  EXPECT_EQ(procedure->parent_parts[0], "foo");
  EXPECT_EQ(formatAST(*call), "CALL foo.bar() YIELD x");
  assertASTContract(ast);
}

TEST(GQLParser, InlineOptionalCallKeepsInlineProcedureShape) {
  auto ast = parseGraphOrThrow("OPTIONAL CALL { RETURN 1 } RETURN *");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* call = getCallInlineClause(*clauses, 0);
  ASSERT_NE(call, nullptr);
  EXPECT_TRUE(call->optional);
  EXPECT_EQ(call->variable_scope, nullptr);

  const auto* procedure = getSubquery(call->subquery);
  ASSERT_NE(procedure, nullptr);
  ASSERT_NE(getClausesQuery(procedure->query), nullptr);
  EXPECT_EQ(formatAST(*call), "OPTIONAL CALL { RETURN 1 }");
}

TEST(GQLParser, InlineCallVariableScopeSupportsEmptyScope) {
  auto ast = parseGraphOrThrow("CALL () { MATCH (a) RETURN a } RETURN *");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* call = getCallInlineClause(*clauses, 0);
  ASSERT_NE(call, nullptr);
  const auto* variable_scope = getCallVariableScopeClause(call->variable_scope);
  ASSERT_NE(variable_scope, nullptr);
  EXPECT_TRUE(variable_scope->variables.empty());

  const auto* subquery = getSubquery(call->subquery);
  ASSERT_NE(subquery, nullptr);
  ASSERT_NE(getClausesQuery(subquery->query), nullptr);

  auto cloned = call->clone();
  EXPECT_EQ(formatAST(*call), "CALL () { MATCH (a) RETURN a }");
  EXPECT_EQ(formatAST(*cloned), "CALL () { MATCH (a) RETURN a }");
}

TEST(GQLParser, InlineCallVariableScopeSupportsNamedBindings) {
  auto ast = parseGraphOrThrow("CALL (a, b) { RETURN a, b } RETURN *");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* call = getCallInlineClause(*clauses, 0);
  ASSERT_NE(call, nullptr);

  const auto* variable_scope = getCallVariableScopeClause(call->variable_scope);
  ASSERT_NE(variable_scope, nullptr);
  ASSERT_EQ(variable_scope->variables.size(), 2);
  EXPECT_EQ(getExpr(variable_scope->variables[0])->text, "a");
  EXPECT_EQ(getExpr(variable_scope->variables[1])->text, "b");

  const auto* subquery = getSubquery(call->subquery);
  ASSERT_NE(subquery, nullptr);
  const auto* nested_query = getClausesQuery(subquery->query);
  ASSERT_NE(nested_query, nullptr);
  ASSERT_NE(getReturnClause(*nested_query, 0), nullptr);

  EXPECT_EQ(formatAST(*call), "CALL (a, b) { RETURN a, b }");
}

TEST(GQLParser, LetClauseBuildsStructuredItems) {
  auto ast = parseGraphOrThrow("LET x = 1, VALUE y INT = 2 RETURN x");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* let_clause = getLetClause(*clauses, 0);
  ASSERT_NE(let_clause, nullptr);
  ASSERT_EQ(let_clause->items.size(), 2);

  const auto* binding_assignment = getAssignmentItem(let_clause->items[0]);
  ASSERT_NE(binding_assignment, nullptr);
  EXPECT_EQ(binding_assignment->name, "x");
  EXPECT_FALSE(binding_assignment->value_keyword);
  EXPECT_EQ(binding_assignment->type, nullptr);
  EXPECT_EQ(getExpr(binding_assignment->value)->text, "1");

  const auto* value_assignment = getAssignmentItem(let_clause->items[1]);
  ASSERT_NE(value_assignment, nullptr);
  EXPECT_EQ(value_assignment->name, "y");
  EXPECT_TRUE(value_assignment->value_keyword);
  ASSERT_NE(value_assignment->type, nullptr);
  const auto* assignment_type = value_assignment->type->as<GAST::GQLTypeExpression>();
  ASSERT_NE(assignment_type, nullptr);
  EXPECT_EQ(assignment_type->kind, GAST::GQLTypeExpression::Kind::Name);
  EXPECT_EQ(assignment_type->name, "INT");
  EXPECT_EQ(getExpr(value_assignment->value)->text, "2");
  EXPECT_EQ(formatAST(*let_clause), "LET x = 1, VALUE y INT = 2");
}

TEST(GQLParser, ForClauseBuildsStructuredFields) {
  auto ast = parseGraphOrThrow("FOR x IN [1, 2] WITH OFFSET i RETURN x");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* for_clause = getForClause(*clauses, 0);
  ASSERT_NE(for_clause, nullptr);
  EXPECT_EQ(for_clause->alias, "x");
  EXPECT_FALSE(for_clause->with_ordinality);
  EXPECT_TRUE(for_clause->with_offset);
  EXPECT_EQ(for_clause->ordinality_or_offset_alias, "i");

  const auto* source = getListConstructor(for_clause->source);
  ASSERT_NE(source, nullptr);
  ASSERT_EQ(source->items.size(), 2);
  EXPECT_EQ(getExpr(source->items[0])->text, "1");
  EXPECT_EQ(getExpr(source->items[1])->text, "2");
  EXPECT_EQ(formatAST(*for_clause), "FOR x IN [1, 2] WITH OFFSET i");
}

TEST(GQLParser, FinishClauseBuildsStructuredNode) {
  auto ast = parseGraphOrThrow("MATCH (a) FINISH");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* match = getMatchClause(*clauses, 0);
  ASSERT_NE(match, nullptr);
  const auto* finish_clause = getFinishClause(*clauses, 1);
  ASSERT_NE(finish_clause, nullptr);
  EXPECT_EQ(formatAST(*finish_clause), "FINISH");
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) FINISH");
}

TEST(GQLParser, StandalonePagingBuildsStructuredClause) {
  auto ast = parseGraphOrThrow("MATCH (a) ORDER BY a DESC OFFSET 1 LIMIT 2 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);

  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);

  const auto* page_clause = getPageClause(*clauses, 1);
  ASSERT_NE(page_clause, nullptr);

  const auto* order_by = page_clause->order_by->as<GAST::GQLOrderByClause>();
  ASSERT_NE(order_by, nullptr);
  const auto* offset = getExpr(page_clause->offset);
  const auto* limit = getExpr(page_clause->limit);
  ASSERT_NE(offset, nullptr);
  ASSERT_NE(limit, nullptr);
  EXPECT_EQ(offset->text, "1");
  EXPECT_EQ(limit->text, "2");
  EXPECT_EQ(formatAST(*page_clause), "ORDER BY a DESC OFFSET 1 LIMIT 2");

  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) ORDER BY a DESC OFFSET 1 LIMIT 2 RETURN a");
}

TEST(GQLParser, ReturnOrderByPreservesNullsFirst) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN a ORDER BY a.name NULLS FIRST");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);

  const auto* page_clause = getPageClause(*clauses, 2);
  ASSERT_NE(page_clause, nullptr);
  const auto* order_by = page_clause->order_by->as<GAST::GQLOrderByClause>();
  ASSERT_NE(order_by, nullptr);
  ASSERT_EQ(order_by->items.size(), 1);

  const auto* item = order_by->items[0]->as<GAST::GQLOrderByItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_FALSE(item->descending);
  EXPECT_EQ(item->null_ordering, GAST::GQLOrderByItem::NullOrdering::NullsFirst);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN a ORDER BY a.name NULLS FIRST");
  assertASTContract(ast);
}

TEST(GQLParser, SelectOrderByPreservesNullsLastWithDescending) {
  auto ast = parseGraphOrThrow("SELECT a FROM { MATCH (a) RETURN a } ORDER BY a DESC NULLS LAST");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* page_clause = getPageClause(*clauses, 1);
  ASSERT_NE(page_clause, nullptr);
  const auto* order_by = page_clause->order_by->as<GAST::GQLOrderByClause>();
  ASSERT_NE(order_by, nullptr);
  ASSERT_EQ(order_by->items.size(), 1);

  const auto* item = order_by->items[0]->as<GAST::GQLOrderByItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_TRUE(item->descending);
  EXPECT_EQ(item->null_ordering, GAST::GQLOrderByItem::NullOrdering::NullsLast);
  EXPECT_EQ(formatAST(*clauses), "SELECT a FROM { MATCH (a) RETURN a } ORDER BY a DESC NULLS LAST");
  assertASTContract(ast);
}

TEST(GQLParser, StandaloneOrderByPreservesNullsLast) {
  auto ast = parseGraphOrThrow("MATCH (a) ORDER BY a NULLS LAST RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);

  const auto* page_clause = getPageClause(*clauses, 1);
  ASSERT_NE(page_clause, nullptr);
  const auto* order_by = page_clause->order_by->as<GAST::GQLOrderByClause>();
  ASSERT_NE(order_by, nullptr);
  ASSERT_EQ(order_by->items.size(), 1);

  const auto* item = order_by->items[0]->as<GAST::GQLOrderByItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_FALSE(item->descending);
  EXPECT_EQ(item->null_ordering, GAST::GQLOrderByItem::NullOrdering::NullsLast);
  EXPECT_EQ(formatAST(*page_clause), "ORDER BY a NULLS LAST");
  assertASTContract(ast);
}

TEST(GQLParser, RightEdgeWithRangeQuantifier) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e:KNOWS]->{2,5}(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);

  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Right);

  const auto* variable = getExpr(edge->variable);
  ASSERT_NE(variable, nullptr);
  EXPECT_EQ(variable->text, "e");

  const auto* label = getLabelExpr(edge->label_expression);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->text, "KNOWS");

  const auto* quantifier = getQuantifier(edge->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Range);
  EXPECT_EQ(quantifier->lower, "2");
  EXPECT_EQ(quantifier->upper, "5");
}

TEST(GQLParser, EdgeDirectionLeft) {
  auto ast = parseGraphOrThrow("MATCH (a)<-[e]-(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Left);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)<-[e]-(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)<-[e]-(b) RETURN e");
}

TEST(GQLParser, EdgeDirectionUndirected) {
  auto ast = parseGraphOrThrow("MATCH (a)~[e]~(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Undirected);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)~[e]~(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)~[e]~(b) RETURN e");
}

TEST(GQLParser, EdgeDirectionLeftOrUndirected) {
  auto ast = parseGraphOrThrow("MATCH (a)<~[e]~(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::LeftOrUndirected);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)<~[e]~(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)<~[e]~(b) RETURN e");
}

TEST(GQLParser, EdgeDirectionUndirectedOrRight) {
  auto ast = parseGraphOrThrow("MATCH (a)~[e]~>(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::UndirectedOrRight);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)~[e]~>(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)~[e]~>(b) RETURN e");
}

TEST(GQLParser, EdgeDirectionLeftOrRight) {
  auto ast = parseGraphOrThrow("MATCH (a)<-[e]->(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::LeftOrRight);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)<-[e]->(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)<-[e]->(b) RETURN e");
}

TEST(GQLParser, EdgeDirectionAny) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]-(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Any);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]-(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)-[e]-(b) RETURN e");
}

TEST(GQLParser, AbbreviatedEdgeLeft) {
  auto ast = parseGraphOrThrow("MATCH (a)<-(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Left);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)<-[]-(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)<-[]-(b) RETURN a");
}

TEST(GQLParser, AbbreviatedEdgeUndirected) {
  auto ast = parseGraphOrThrow("MATCH (a)~(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Undirected);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)~[]~(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)~[]~(b) RETURN a");
}

TEST(GQLParser, AbbreviatedEdgeRight) {
  auto ast = parseGraphOrThrow("MATCH (a)->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Right);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[]->(b) RETURN a");
}

TEST(GQLParser, AbbreviatedEdgeLeftOrUndirected) {
  auto ast = parseGraphOrThrow("MATCH (a)<~(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::LeftOrUndirected);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)<~[]~(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)<~[]~(b) RETURN a");
}

TEST(GQLParser, AbbreviatedEdgeUndirectedOrRight) {
  auto ast = parseGraphOrThrow("MATCH (a)~>(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::UndirectedOrRight);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)~[]~>(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)~[]~>(b) RETURN a");
}

TEST(GQLParser, AbbreviatedEdgeLeftOrRight) {
  auto ast = parseGraphOrThrow("MATCH (a)<->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::LeftOrRight);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)<-[]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)<-[]->(b) RETURN a");
}

TEST(GQLParser, AbbreviatedEdgeAny) {
  auto ast = parseGraphOrThrow("MATCH (a)-(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Any);
  EXPECT_EQ(edge->variable, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[]-(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[]-(b) RETURN a");
}

TEST(GQLParser, NodePropertyMap) {
  auto ast = parseGraphOrThrow("MATCH (n:Person {name: 'Alice', age: 42}) RETURN n");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);
  const auto* node = term->factors[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(node, nullptr);
  ASSERT_NE(node->properties, nullptr);
  const auto* prop_map = node->properties->as<GAST::GQLPropertyMap>();
  ASSERT_NE(prop_map, nullptr);
  ASSERT_EQ(prop_map->items.size(), 2);
  const auto* item0 = prop_map->items[0]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(item0, nullptr);
  EXPECT_EQ(item0->key, "name");
  const auto* item1 = prop_map->items[1]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(item1, nullptr);
  EXPECT_EQ(item1->key, "age");
  ASSERT_NE(item0->value, nullptr);
  const auto* val0 = getExpr(item0->value);
  ASSERT_NE(val0, nullptr);
  EXPECT_EQ(val0->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val0->text, "'Alice'");
  ASSERT_NE(item1->value, nullptr);
  const auto* val1 = getExpr(item1->value);
  ASSERT_NE(val1, nullptr);
  EXPECT_EQ(val1->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val1->text, "42");
  EXPECT_NE(node->label_expression, nullptr);
  EXPECT_EQ(node->where, nullptr);
  EXPECT_TRUE(std::find(node->children.begin(), node->children.end(), node->properties) != node->children.end());
  auto cloned = clauses->clone();
  const auto* cloned_clauses = cloned->as<GAST::GQLSingleQuery>();
  ASSERT_NE(cloned_clauses, nullptr);
  const auto* cloned_match = getMatchClause(*cloned_clauses);
  ASSERT_NE(cloned_match, nullptr);
  const auto* cloned_path = getOnlyPathPattern(*cloned_match);
  ASSERT_NE(cloned_path, nullptr);
  const auto* cloned_term = getPathTerm(*cloned_path);
  ASSERT_NE(cloned_term, nullptr);
  const auto* cloned_node = cloned_term->factors[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(cloned_node, nullptr);
  ASSERT_NE(cloned_node->properties, nullptr);
  EXPECT_NE(cloned_node->properties.get(), node->properties.get());
  const auto* cloned_map = cloned_node->properties->as<GAST::GQLPropertyMap>();
  ASSERT_NE(cloned_map, nullptr);
  ASSERT_EQ(cloned_map->items.size(), 2);
  EXPECT_NE(cloned_map->items[0].get(), prop_map->items[0].get());
  const auto* cloned_item0 = cloned_map->items[0]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(cloned_item0, nullptr);
  EXPECT_NE(cloned_item0->value.get(), item0->value.get());
  EXPECT_EQ(formatAST(*clauses), "MATCH (n:Person {name: 'Alice', age: 42}) RETURN n");
  assertNormalizedRoundTrip("MATCH (n:Person {name: 'Alice', age: 42}) RETURN n");
}

TEST(GQLParser, EdgePropertyMap) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e:KNOWS {since: 2020}]->(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Right);
  ASSERT_NE(edge->variable, nullptr);
  ASSERT_NE(edge->label_expression, nullptr);
  ASSERT_NE(edge->properties, nullptr);
  EXPECT_EQ(edge->where, nullptr);
  const auto* prop_map = edge->properties->as<GAST::GQLPropertyMap>();
  ASSERT_NE(prop_map, nullptr);
  ASSERT_EQ(prop_map->items.size(), 1);
  const auto* item = prop_map->items[0]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->key, "since");
  ASSERT_NE(item->value, nullptr);
  const auto* val = getExpr(item->value);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val->text, "2020");
  EXPECT_TRUE(std::find(edge->children.begin(), edge->children.end(), edge->properties) != edge->children.end());
  auto cloned = clauses->clone();
  const auto* cloned_clauses = cloned->as<GAST::GQLSingleQuery>();
  ASSERT_NE(cloned_clauses, nullptr);
  const auto* cloned_match = getMatchClause(*cloned_clauses);
  ASSERT_NE(cloned_match, nullptr);
  const auto* cloned_path = getOnlyPathPattern(*cloned_match);
  ASSERT_NE(cloned_path, nullptr);
  const auto* cloned_term = getPathTerm(*cloned_path);
  ASSERT_NE(cloned_term, nullptr);
  const auto* cloned_edge = cloned_term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(cloned_edge, nullptr);
  ASSERT_NE(cloned_edge->properties, nullptr);
  EXPECT_NE(cloned_edge->properties.get(), edge->properties.get());
  const auto* cloned_map = cloned_edge->properties->as<GAST::GQLPropertyMap>();
  ASSERT_NE(cloned_map, nullptr);
  ASSERT_EQ(cloned_map->items.size(), 1);
  EXPECT_NE(cloned_map->items[0].get(), prop_map->items[0].get());
  const auto* cloned_item = cloned_map->items[0]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(cloned_item, nullptr);
  EXPECT_NE(cloned_item->value.get(), item->value.get());
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e:KNOWS {since: 2020}]->(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)-[e:KNOWS {since: 2020}]->(b) RETURN e");
}

TEST(GQLParser, NodeLocalWhere) {
  auto ast = parseGraphOrThrow("MATCH (n WHERE n.age > 30) RETURN n");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);
  const auto* node = term->factors[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->properties, nullptr);
  ASSERT_NE(node->where, nullptr);
  const auto* where = node->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  EXPECT_EQ(match->where, nullptr);
  EXPECT_TRUE(std::find(node->children.begin(), node->children.end(), node->where) != node->children.end());
  auto cloned = clauses->clone();
  const auto* cloned_clauses = cloned->as<GAST::GQLSingleQuery>();
  ASSERT_NE(cloned_clauses, nullptr);
  const auto* cloned_match = getMatchClause(*cloned_clauses);
  ASSERT_NE(cloned_match, nullptr);
  const auto* cloned_path = getOnlyPathPattern(*cloned_match);
  ASSERT_NE(cloned_path, nullptr);
  const auto* cloned_term = getPathTerm(*cloned_path);
  ASSERT_NE(cloned_term, nullptr);
  const auto* cloned_node = cloned_term->factors[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(cloned_node, nullptr);
  EXPECT_NE(cloned_node->where.get(), node->where.get());
  EXPECT_EQ(formatAST(*clauses), "MATCH (n WHERE (n.age > 30)) RETURN n");
  assertNormalizedRoundTrip("MATCH (n WHERE (n.age > 30)) RETURN n");
}

TEST(GQLParser, EdgeLocalWhere) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e WHERE e.weight > 0]->(b) RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Right);
  EXPECT_EQ(edge->properties, nullptr);
  ASSERT_NE(edge->where, nullptr);
  const auto* where = edge->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  EXPECT_EQ(match->where, nullptr);
  EXPECT_TRUE(std::find(edge->children.begin(), edge->children.end(), edge->where) != edge->children.end());
  auto cloned = clauses->clone();
  const auto* cloned_clauses = cloned->as<GAST::GQLSingleQuery>();
  ASSERT_NE(cloned_clauses, nullptr);
  const auto* cloned_match = getMatchClause(*cloned_clauses);
  ASSERT_NE(cloned_match, nullptr);
  const auto* cloned_path = getOnlyPathPattern(*cloned_match);
  ASSERT_NE(cloned_path, nullptr);
  const auto* cloned_term = getPathTerm(*cloned_path);
  ASSERT_NE(cloned_term, nullptr);
  const auto* cloned_edge = cloned_term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(cloned_edge, nullptr);
  EXPECT_NE(cloned_edge->where.get(), edge->where.get());
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e WHERE (e.weight > 0)]->(b) RETURN e");
  assertNormalizedRoundTrip("MATCH (a)-[e WHERE (e.weight > 0)]->(b) RETURN e");
}

TEST(GQLParser, LabelConjunction) {
  auto ast = parseGraphOrThrow("MATCH (n:Person&Employee) RETURN n");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* node = term->factors[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(node, nullptr);

  const auto* conjunction = getLabelExpr(node->label_expression);
  ASSERT_NE(conjunction, nullptr);
  EXPECT_EQ(conjunction->kind, GAST::GQLLabelExpression::Kind::Conjunction);
  ASSERT_EQ(conjunction->children.size(), 2);

  const auto* left = getLabelExpr(conjunction->children[0]);
  const auto* right = getLabelExpr(conjunction->children[1]);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(left->text, "Person");
  EXPECT_EQ(right->text, "Employee");
}

TEST(GQLParser, WhereExpressionKeepsStructure) {
  auto ast = parseGraphOrThrow("MATCH (a:Person) WHERE a.age > 20 AND a.age < 40 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* root = getExpr(where->expression);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(root->text, "AND");
  ASSERT_EQ(root->children.size(), 2);

  const auto* left = getExpr(root->children[0]);
  const auto* right = getExpr(root->children[1]);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(left->text, ">");
  EXPECT_EQ(right->text, "<");
}

TEST(GQLParser, PropertyAccessExpression) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name = 'Bob' RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* equals = getExpr(where->expression);
  ASSERT_NE(equals, nullptr);
  EXPECT_EQ(equals->text, "=");
  ASSERT_EQ(equals->children.size(), 2);

  const auto* property = getExpr(equals->children[0]);
  ASSERT_NE(property, nullptr);
  EXPECT_EQ(property->kind, GAST::GQLExpr::Kind::Property);
  EXPECT_EQ(property->text, "name");
  ASSERT_EQ(property->children.size(), 1);

  const auto* identifier = getExpr(property->children[0]);
  ASSERT_NE(identifier, nullptr);
  EXPECT_EQ(identifier->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(identifier->text, "a");

  const auto* literal = getExpr(equals->children[1]);
  ASSERT_NE(literal, nullptr);
  EXPECT_EQ(literal->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(literal->text, "'Bob'");
}

TEST(GQLParser, TruthValuePredicateKeepsStructure) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a IS NOT TRUE RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* predicate = getExpr(where->expression);
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(predicate->text, "IS NOT");

  const auto* left = getExpr(predicate->children[0]);
  const auto* right = getExpr(predicate->children[1]);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(left->text, "a");
  EXPECT_EQ(right->kind, GAST::GQLExpr::Kind::SpecialValue);
  EXPECT_EQ(right->text, "TRUE");
}

TEST(GQLParser, NullPredicateKeepsStructure) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name IS NOT NULL RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* predicate = getExpr(where->expression);
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->text, "IS NOT");

  const auto* property = getExpr(predicate->children[0]);
  const auto* null_literal = getExpr(predicate->children[1]);
  ASSERT_NE(property, nullptr);
  ASSERT_NE(null_literal, nullptr);
  EXPECT_EQ(property->kind, GAST::GQLExpr::Kind::Property);
  EXPECT_EQ(property->text, "name");
  EXPECT_EQ(null_literal->kind, GAST::GQLExpr::Kind::SpecialValue);
  EXPECT_EQ(null_literal->text, "NULL");
}

TEST(GQLParser, DirectedPredicateMarkerStaysLiteral) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) WHERE e IS DIRECTED RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* predicate = getExpr(where->expression);
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->text, "IS");
  ASSERT_EQ(predicate->children.size(), 2);

  const auto* right = getExpr(predicate->children[1]);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(right->text, "DIRECTED");
}

TEST(GQLParser, ValueTypePredicateBuildsTypeAst) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a IS TYPED STRING RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* predicate = getExpr(where->expression);
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->text, "IS TYPED");
  ASSERT_EQ(predicate->children.size(), 2);

  const auto* right = predicate->children[1]->as<GAST::GQLTypeExpression>();
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->kind, GAST::GQLTypeExpression::Kind::Name);
  EXPECT_EQ(right->name, "STRING");
}

TEST(GQLParser, PropertyExistsPredicateBecomesFunctionCall) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE PROPERTY_EXISTS(a, name) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* function = getExpr(where->expression);
  ASSERT_NE(function, nullptr);
  EXPECT_EQ(function->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(function->text, "PROPERTY_EXISTS");
  ASSERT_EQ(function->children.size(), 2);

  const auto* element = getExpr(function->children[0]);
  const auto* property = getExpr(function->children[1]);
  ASSERT_NE(element, nullptr);
  ASSERT_NE(property, nullptr);
  EXPECT_EQ(element->text, "a");
  EXPECT_EQ(property->text, "name");
}

TEST(GQLParser, AllDifferentPredicateBecomesFunctionCall) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) WHERE ALL_DIFFERENT(a, b, e) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* function = getExpr(where->expression);
  ASSERT_NE(function, nullptr);
  EXPECT_EQ(function->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(function->text, "ALL_DIFFERENT");
  ASSERT_EQ(function->children.size(), 3);
}

TEST(GQLParser, SamePredicateBecomesFunctionCall) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) WHERE SAME(a, b, e) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* function = getExpr(where->expression);
  ASSERT_NE(function, nullptr);
  EXPECT_EQ(function->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(function->text, "SAME");
  ASSERT_EQ(function->children.size(), 3);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) WHERE SAME(a, b, e) RETURN a");
}

TEST(GQLParser, NegatedPredicatePartsKeepOperators) {
  auto ast = parseGraphOrThrow(
      "MATCH (a)-[e]->(b) WHERE (a IS NOT TYPED STRING) AND (e IS NOT DIRECTED) AND (a IS NOT LABELED Person) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* root = getExpr(where->expression);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(root->text, "AND");
  EXPECT_EQ(formatAST(*clauses),
            "MATCH (a)-[e]->(b) WHERE (((a IS NOT TYPED STRING) AND (e IS NOT DIRECTED)) AND (a IS NOT LABELED Person)) RETURN a");
}

TEST(GQLParser, SourceDestinationPredicatesKeepOperators) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) WHERE (a IS NOT SOURCE OF e) OR (b IS DESTINATION OF e) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* root = getExpr(where->expression);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(root->text, "OR");
  EXPECT_EQ(formatAST(*clauses),
            "MATCH (a)-[e]->(b) WHERE ((a IS NOT SOURCE OF e) OR (b IS DESTINATION OF e)) RETURN a");
}

TEST(GQLParser, ParenthesizedExistsPredicateKeepsBlockShape) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS ((a)-[e]->(b)) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);

  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  ASSERT_EQ(exists->children.size(), 1);
  const auto* block = getGraphPatternBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  EXPECT_TRUE(block->parenthesized);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS ((a)-[e]->(b)) RETURN a");
}

TEST(GQLParser, ReturnClauseKeepsAliasAndTail) {
  auto ast = parseGraphOrThrow("MATCH (a:Person) RETURN a.name AS name, a.age ORDER BY a.age DESC OFFSET 2 LIMIT 5");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);

  const auto* return_clause = getReturnClause(*clauses);
  ASSERT_NE(return_clause, nullptr);
  EXPECT_FALSE(return_clause->distinct);
  ASSERT_EQ(return_clause->items.size(), 2);

  const auto* aliased = getAliasedItem(return_clause->items[0]);
  ASSERT_NE(aliased, nullptr);
  EXPECT_EQ(aliased->alias, "name");

  const auto* expression = getExpr(aliased->expression);
  ASSERT_NE(expression, nullptr);
  EXPECT_EQ(expression->kind, GAST::GQLExpr::Kind::Property);

  const auto* page_clause = getPageClause(*clauses, 2);
  ASSERT_NE(page_clause, nullptr);

  const auto* order_by = page_clause->order_by->as<GAST::GQLOrderByClause>();
  ASSERT_NE(order_by, nullptr);
  ASSERT_EQ(order_by->items.size(), 1);

  const auto* order_item = order_by->items[0]->as<GAST::GQLOrderByItem>();
  ASSERT_NE(order_item, nullptr);
  EXPECT_TRUE(order_item->descending);

  const auto* offset = getExpr(page_clause->offset);
  const auto* limit = getExpr(page_clause->limit);
  ASSERT_NE(offset, nullptr);
  ASSERT_NE(limit, nullptr);
  EXPECT_EQ(offset->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(limit->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(offset->text, "2");
  EXPECT_EQ(limit->text, "5");
}

TEST(GQLParser, ReturnListConstructorAliasUsesAliasedItem) {
  auto ast = parseGraphOrThrow("RETURN [1, 2] AS xs");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* return_clause = getReturnClause(*clauses);
  ASSERT_NE(return_clause, nullptr);
  ASSERT_EQ(return_clause->items.size(), 1);

  const auto* item = getAliasedItem(return_clause->items[0]);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->alias, "xs");
  ASSERT_EQ(item->children.size(), 1);
  EXPECT_EQ(item->children[0].get(), item->expression.get());

  const auto* list = getListConstructor(item->expression);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->items.size(), 2);
  EXPECT_EQ(getExpr(list->items[0])->text, "1");
  EXPECT_EQ(getExpr(list->items[1])->text, "2");

  auto cloned = item->clone();
  const auto* cloned_item = getAliasedItem(cloned);
  ASSERT_NE(cloned_item, nullptr);
  EXPECT_EQ(cloned_item->alias, "xs");
  ASSERT_NE(cloned_item->expression, nullptr);
  EXPECT_NE(cloned_item->expression.get(), item->expression.get());
  ASSERT_EQ(cloned_item->children.size(), 1);
  EXPECT_EQ(formatAST(*item), "[1, 2] AS xs");
}

TEST(GQLParser, ReturnRecordConstructorAliasUsesAliasedItem) {
  auto ast = parseGraphOrThrow("RETURN RECORD {x: 1} AS r");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* return_clause = getReturnClause(*clauses);
  ASSERT_NE(return_clause, nullptr);
  ASSERT_EQ(return_clause->items.size(), 1);

  const auto* item = getAliasedItem(return_clause->items[0]);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->alias, "r");
  ASSERT_EQ(item->children.size(), 1);

  const auto* record = getRecordConstructor(item->expression);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->explicit_record_keyword);
  ASSERT_EQ(record->fields.size(), 1);

  const auto* field = record->fields[0]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(field, nullptr);
  EXPECT_EQ(field->key, "x");
  EXPECT_EQ(getExpr(field->value)->text, "1");
  EXPECT_EQ(formatAST(*item), "RECORD {x: 1} AS r");
}

TEST(GQLParser, SelectListConstructorAliasUsesAliasedItem) {
  auto ast = parseGraphOrThrow("SELECT [1] AS xs FROM { RETURN 1 }");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* select_clause = getSelectClause(*clauses, 0);
  ASSERT_NE(select_clause, nullptr);
  ASSERT_EQ(select_clause->items.size(), 1);

  const auto* item = getAliasedItem(select_clause->items[0]);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->alias, "xs");

  const auto* list = getListConstructor(item->expression);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->items.size(), 1);
  EXPECT_EQ(getExpr(list->items[0])->text, "1");

  const auto* source = getSubquery(select_clause->source);
  ASSERT_NE(source, nullptr);
  ASSERT_NE(getClausesQuery(source->query), nullptr);
}

TEST(GQLParser, YieldAliasUsesAliasedItem) {
  auto ast = parseGraphOrThrow("CALL foo() YIELD x AS y");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* call = getCallNamedClause(*clauses, 0);
  ASSERT_NE(call, nullptr);

  const auto* yield_clause = getYieldClause(call->yield);
  ASSERT_NE(yield_clause, nullptr);
  ASSERT_EQ(yield_clause->items.size(), 1);

  const auto* item = getAliasedItem(yield_clause->items[0]);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->alias, "y");

  const auto* expression = getExpr(item->expression);
  ASSERT_NE(expression, nullptr);
  EXPECT_EQ(expression->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(expression->text, "x");
  EXPECT_EQ(formatAST(*call), "CALL foo() YIELD x AS y");
}

TEST(GQLParser, CompositeUnionQuery) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN a UNION MATCH (b) RETURN b");
  const auto* combined_query = ast->as<GAST::GQLCombinedQuery>();
  ASSERT_NE(combined_query, nullptr);
  ASSERT_EQ(combined_query->queries.size(), 2);
  ASSERT_EQ(combined_query->operators.size(), 1);
  EXPECT_EQ(combined_query->operators[0], GAST::CombinedQueryOperator::UnionDistinct);
  EXPECT_NE(combined_query->queries[0].get(), nullptr);
  EXPECT_NE(combined_query->queries[1].get(), nullptr);
}

TEST(GQLParser, CompositeUnionQueryKeepsOperatorSequence) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN a UNION ALL MATCH (b) RETURN b EXCEPT MATCH (c) RETURN c");
  const auto* combined_query = ast->as<GAST::GQLCombinedQuery>();
  ASSERT_NE(combined_query, nullptr);
  ASSERT_EQ(combined_query->queries.size(), 3);
  ASSERT_EQ(combined_query->operators.size(), 2);
  EXPECT_EQ(combined_query->operators[0], GAST::CombinedQueryOperator::UnionAll);
  EXPECT_EQ(combined_query->operators[1], GAST::CombinedQueryOperator::ExceptDistinct);
  EXPECT_EQ(formatAST(*combined_query), "MATCH (a) RETURN a UNION ALL MATCH (b) RETURN b EXCEPT MATCH (c) RETURN c");
}

TEST(GQLParser, NestedQuerySpecificationKeepsWrapper) {
  auto ast = parseGraphOrThrow("{ MATCH (a) RETURN a }");
  const auto* subquery = getSubquery(ast);
  ASSERT_NE(subquery, nullptr);

  const auto* clauses = getClausesQuery(subquery->query);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);
  ASSERT_NE(getMatchClause(*clauses), nullptr);
  ASSERT_NE(getReturnClause(*clauses), nullptr);
  EXPECT_EQ(formatAST(*subquery), "{ MATCH (a) RETURN a }");
}

TEST(GQLParser, StandaloneOrderByStatementKeepsStructuredPageClause) {
  auto ast = parseGraphOrThrow("MATCH (a) ORDER BY a DESC LIMIT 3 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);

  const auto* page = getPageClause(*clauses, 1);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(formatAST(*page), "ORDER BY a DESC LIMIT 3");
}

TEST(GQLParser, StandaloneOffsetOnly) {
  auto ast = parseGraphOrThrow("MATCH (a) OFFSET 2 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);
  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);
  const auto* page = getPageClause(*clauses, 1);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->order_by, nullptr);
  ASSERT_NE(page->offset, nullptr);
  EXPECT_EQ(getExpr(page->offset)->text, "2");
  EXPECT_EQ(page->limit, nullptr);
  EXPECT_EQ(formatAST(*page), "OFFSET 2");
  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  assertNormalizedRoundTrip("MATCH (a) OFFSET 2 RETURN a");
}

TEST(GQLParser, StandaloneLimitOnly) {
  auto ast = parseGraphOrThrow("MATCH (a) LIMIT 5 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);
  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);
  const auto* page = getPageClause(*clauses, 1);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->order_by, nullptr);
  EXPECT_EQ(page->offset, nullptr);
  ASSERT_NE(page->limit, nullptr);
  EXPECT_EQ(getExpr(page->limit)->text, "5");
  EXPECT_EQ(formatAST(*page), "LIMIT 5");
  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  assertNormalizedRoundTrip("MATCH (a) LIMIT 5 RETURN a");
}

TEST(GQLParser, StandaloneOffsetAndLimit) {
  auto ast = parseGraphOrThrow("MATCH (a) OFFSET 2 LIMIT 5 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 3);
  ASSERT_NE(getMatchClause(*clauses, 0), nullptr);
  const auto* page = getPageClause(*clauses, 1);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->order_by, nullptr);
  ASSERT_NE(page->offset, nullptr);
  EXPECT_EQ(getExpr(page->offset)->text, "2");
  ASSERT_NE(page->limit, nullptr);
  EXPECT_EQ(getExpr(page->limit)->text, "5");
  EXPECT_EQ(formatAST(*page), "OFFSET 2 LIMIT 5");
  ASSERT_NE(getReturnClause(*clauses, 2), nullptr);
  assertNormalizedRoundTrip("MATCH (a) OFFSET 2 LIMIT 5 RETURN a");
}

TEST(GQLParser, FocusedUseLimitOnly) {
  auto ast = parseGraphOrThrow("USE foo MATCH (a) LIMIT 5 RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 4);
  ASSERT_NE(getUseClause(*clauses, 0), nullptr);
  ASSERT_NE(getMatchClause(*clauses, 1), nullptr);
  const auto* page = getPageClause(*clauses, 2);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(formatAST(*page), "LIMIT 5");
  ASSERT_NE(getReturnClause(*clauses, 3), nullptr);
  assertNormalizedRoundTrip("USE foo MATCH (a) LIMIT 5 RETURN a");
}

TEST(GQLParser, FinishStatementKeepsStructuredClause) {
  auto ast = parseGraphOrThrow("MATCH (a) FINISH");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  ASSERT_NE(getFinishClause(*clauses, 1), nullptr);
}

TEST(GQLParser, ReturnClauseKeepsGroupBy) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN a GROUP BY a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* return_clause = getReturnClause(*clauses);
  ASSERT_NE(return_clause, nullptr);

  const auto* group_by = getGroupByClause(return_clause->group_by);
  ASSERT_NE(group_by, nullptr);
  ASSERT_EQ(group_by->items.size(), 1);
  EXPECT_EQ(getExpr(group_by->items[0])->text, "a");
  EXPECT_EQ(formatAST(*return_clause), "RETURN a GROUP BY a");
}

TEST(GQLParser, ReturnClauseSupportsEmptyGroupingSet) {
  auto ast = parseGraphOrThrow("RETURN a GROUP BY ()");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* return_clause = getReturnClause(*clauses, 0);
  ASSERT_NE(return_clause, nullptr);

  const auto* group_by = getGroupByClause(return_clause->group_by);
  ASSERT_NE(group_by, nullptr);
  EXPECT_TRUE(group_by->empty_grouping_set);
  EXPECT_TRUE(group_by->items.empty());
  EXPECT_EQ(formatAST(*return_clause), "RETURN a GROUP BY ()");
}

TEST(GQLParser, InlineCallVariableScopeSupportsSingleBinding) {
  auto ast = parseGraphOrThrow("CALL (x) { RETURN x } RETURN x");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* call = getCallInlineClause(*clauses, 0);
  ASSERT_NE(call, nullptr);

  const auto* variable_scope = getCallVariableScopeClause(call->variable_scope);
  ASSERT_NE(variable_scope, nullptr);
  ASSERT_EQ(variable_scope->variables.size(), 1);
  EXPECT_EQ(getExpr(variable_scope->variables[0])->text, "x");
  EXPECT_EQ(formatAST(*call), "CALL (x) { RETURN x }");
}

TEST(GQLParser, MatchParsesPathModePrefix) {
  auto ast = parseGraphOrThrow("MATCH TRAIL (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);

  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathModePrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Trail);
  EXPECT_EQ(formatAST(*clauses), "MATCH TRAIL (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, MatchPathModeWalk) {
  auto ast = parseGraphOrThrow("MATCH WALK (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathModePrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Walk);
  EXPECT_EQ(formatAST(*clauses), "MATCH WALK (a)-[e]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH WALK (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, MatchPathModeSimple) {
  auto ast = parseGraphOrThrow("MATCH SIMPLE (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathModePrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Simple);
  EXPECT_EQ(formatAST(*clauses), "MATCH SIMPLE (a)-[e]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH SIMPLE (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, MatchPathModeAcyclic) {
  auto ast = parseGraphOrThrow("MATCH ACYCLIC (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathModePrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Acyclic);
  EXPECT_EQ(formatAST(*clauses), "MATCH ACYCLIC (a)-[e]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH ACYCLIC (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, KeepPathModeWalk) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP WALK RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_NE(match->keep_clause, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);
  const auto* path_prefix = getPathModePrefix(keep->path_prefix);
  ASSERT_NE(path_prefix, nullptr);
  EXPECT_EQ(path_prefix->path_mode, GAST::PathMode::Walk);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP WALK RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->(b) KEEP WALK RETURN a");
}

TEST(GQLParser, KeepPathModeSimple) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP SIMPLE RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_NE(match->keep_clause, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);
  const auto* path_prefix = getPathModePrefix(keep->path_prefix);
  ASSERT_NE(path_prefix, nullptr);
  EXPECT_EQ(path_prefix->path_mode, GAST::PathMode::Simple);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP SIMPLE RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->(b) KEEP SIMPLE RETURN a");
}

TEST(GQLParser, KeepPathModeAcyclic) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP ACYCLIC RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_NE(match->keep_clause, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);
  const auto* path_prefix = getPathModePrefix(keep->path_prefix);
  ASSERT_NE(path_prefix, nullptr);
  EXPECT_EQ(path_prefix->path_mode, GAST::PathMode::Acyclic);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP ACYCLIC RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->(b) KEEP ACYCLIC RETURN a");
}

TEST(GQLParser, MatchParsesPathSearchPrefixAnyShortest) {
  auto ast = parseGraphOrThrow("MATCH ANY SHORTEST (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::AnyShortest);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::None);
  EXPECT_EQ(formatAST(*clauses), "MATCH ANY SHORTEST (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, MatchParsesPathSearchPrefixCountedGroups) {
  auto ast = parseGraphOrThrow("MATCH SHORTEST 3 GROUPS (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::CountedShortestGroup);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Groups);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->kind, GAST::CountSpecKind::Integer);
  EXPECT_EQ(count->text, "3");
  EXPECT_TRUE(prefix->use_groups_keyword);
  EXPECT_EQ(formatAST(*clauses), "MATCH SHORTEST 3 GROUPS (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, MatchParsesPathSearchPrefixAnyCountedPaths) {
  auto ast = parseGraphOrThrow("MATCH ANY 3 PATHS (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::Any);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Paths);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->kind, GAST::CountSpecKind::Integer);
  EXPECT_EQ(count->text, "3");
  EXPECT_EQ(formatAST(*clauses), "MATCH ANY 3 PATHS (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, MatchParsesPathSearchPrefixDynamicGroupCount) {
  auto ast = parseGraphOrThrow("MATCH SHORTEST $n GROUPS (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::CountedShortestGroup);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Groups);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->kind, GAST::CountSpecKind::DynamicParameter);
  EXPECT_EQ(count->text, "$n");
  EXPECT_EQ(formatAST(*clauses), "MATCH SHORTEST $n GROUPS (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, PathSearchPrefixCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH SHORTEST 3 GROUPS (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);

  auto cloned = prefix->clone();
  const auto* cloned_prefix = getPathSearchPrefix(cloned);
  ASSERT_NE(cloned_prefix, nullptr);
  EXPECT_NE(cloned_prefix, prefix);
  ASSERT_NE(cloned_prefix->count, nullptr);
  ASSERT_NE(prefix->count, nullptr);
  EXPECT_NE(cloned_prefix->count.get(), prefix->count.get());
  const auto* cloned_count = getCountSpec(cloned_prefix->count);
  ASSERT_NE(cloned_count, nullptr);
  EXPECT_EQ(cloned_count->kind, GAST::CountSpecKind::Integer);
  EXPECT_EQ(cloned_count->text, "3");
  EXPECT_EQ(formatAST(*prefix), "SHORTEST 3 GROUPS");
  EXPECT_EQ(formatAST(*cloned_prefix), "SHORTEST 3 GROUPS");
}

TEST(GQLParser, PathSearchPrefixAll) {
  auto ast = parseGraphOrThrow("MATCH ALL (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::All);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::None);
  EXPECT_EQ(prefix->count, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH ALL (a)-[*]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH ALL (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, PathSearchPrefixAnyNoCount) {
  auto ast = parseGraphOrThrow("MATCH ANY (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::Any);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::None);
  EXPECT_EQ(prefix->count, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH ANY (a)-[*]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH ANY (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, PathSearchPrefixAllShortest) {
  auto ast = parseGraphOrThrow("MATCH ALL SHORTEST (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::AllShortest);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::None);
  EXPECT_EQ(prefix->count, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH ALL SHORTEST (a)-[*]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH ALL SHORTEST (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, PathSearchPrefixCountedShortestPaths) {
  auto ast = parseGraphOrThrow("MATCH SHORTEST 2 PATHS (a)-[*]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* prefix = getPathSearchPrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::CountedShortest);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Paths);
  ASSERT_NE(prefix->count, nullptr);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->kind, GAST::CountSpecKind::Integer);
  EXPECT_EQ(count->text, "2");
  EXPECT_TRUE(prefix->has_path_keyword);
  EXPECT_TRUE(prefix->use_paths_keyword);
  EXPECT_EQ(formatAST(*clauses), "MATCH SHORTEST 2 PATHS (a)-[*]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH SHORTEST 2 PATHS (a)-[*]->(b) RETURN a");
}

TEST(GQLParser, MatchClauseKeepsMatchModeAndYield) {
  auto ast = parseGraphOrThrow("MATCH REPEATABLE ELEMENTS (a)-[e]->(b) YIELD e RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  EXPECT_EQ(match->match_mode, GAST::GraphMatchMode::RepeatableElements);
  ASSERT_EQ(match->yield_items.size(), 1);
  EXPECT_EQ(getExpr(match->yield_items[0])->text, "e");
  EXPECT_EQ(formatAST(*clauses), "MATCH REPEATABLE ELEMENTS (a)-[e]->(b) YIELD e RETURN e");
}

TEST(GQLParser, MatchModeRepeatableElementBindings) {
  auto ast = parseGraphOrThrow("MATCH REPEATABLE ELEMENT BINDINGS (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  EXPECT_EQ(match->match_mode, GAST::GraphMatchMode::RepeatableElementBindings);
  EXPECT_EQ(formatAST(*clauses), "MATCH REPEATABLE ELEMENT BINDINGS (a)-[e]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH REPEATABLE ELEMENT BINDINGS (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, MatchModeDifferentEdges) {
  auto ast = parseGraphOrThrow("MATCH DIFFERENT EDGES (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  EXPECT_EQ(match->match_mode, GAST::GraphMatchMode::DifferentEdges);
  EXPECT_EQ(formatAST(*clauses), "MATCH DIFFERENT EDGES (a)-[e]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH DIFFERENT EDGES (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, MatchModeDifferentEdgeBindings) {
  auto ast = parseGraphOrThrow("MATCH DIFFERENT EDGE BINDINGS (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  EXPECT_EQ(match->match_mode, GAST::GraphMatchMode::DifferentEdgeBindings);
  EXPECT_EQ(formatAST(*clauses), "MATCH DIFFERENT EDGE BINDINGS (a)-[e]->(b) RETURN a");
  assertNormalizedRoundTrip("MATCH DIFFERENT EDGE BINDINGS (a)-[e]->(b) RETURN a");
}

TEST(GQLParser, ExistsBlockMatchModeDifferentEdges) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { DIFFERENT EDGES (a)-[e]->(b) } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_NE(match->where, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  EXPECT_EQ(exists->kind, GAST::GQLExpr::Kind::UnaryOp);
  ASSERT_EQ(exists->children.size(), 1);

  const auto* block = getGraphPatternBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->match_mode, GAST::GraphMatchMode::DifferentEdges);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { DIFFERENT EDGES (a)-[e]->(b) } RETURN a");
  assertNormalizedRoundTrip("MATCH (a) WHERE EXISTS { DIFFERENT EDGES (a)-[e]->(b) } RETURN a");
}

TEST(GQLParser, ParenthesizedPathPatternKeepsQuestionQuantifier) {
  auto ast = parseGraphOrThrow("MATCH ((a)-[e]->(b))? RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  const auto* quantifier = getQuantifier(parenthesized->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Question);
  EXPECT_EQ(formatAST(*clauses), "MATCH ((a)-[e]->(b))? RETURN e");
}

TEST(GQLParser, NodePatternQuantifierBuildsWrapper) {
  auto ast = parseGraphOrThrow("MATCH (a){2} RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* quantified = getQuantifiedPathPrimary(term->factors[0]);
  ASSERT_NE(quantified, nullptr);
  ASSERT_NE(quantified->operand->as<GAST::GQLNodePattern>(), nullptr);
  const auto* quantifier = getQuantifier(quantified->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Exact);
  EXPECT_EQ(quantifier->lower, "2");
  EXPECT_EQ(formatAST(*clauses), "MATCH (a){2} RETURN a");
}

TEST(GQLParser, NodePatternQuestionQuantifierBuildsWrapper) {
  auto ast = parseGraphOrThrow("MATCH (a)? RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* quantified = getQuantifiedPathPrimary(term->factors[0]);
  ASSERT_NE(quantified, nullptr);
  ASSERT_NE(quantified->operand->as<GAST::GQLNodePattern>(), nullptr);
  const auto* quantifier = getQuantifier(quantified->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Question);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)? RETURN a");
}

TEST(GQLParser, NodePatternStarQuantifier) {
  auto ast = parseGraphOrThrow("MATCH (a)* RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* quantified = getQuantifiedPathPrimary(term->factors[0]);
  ASSERT_NE(quantified, nullptr);
  ASSERT_NE(quantified->operand->as<GAST::GQLNodePattern>(), nullptr);
  const auto* quantifier = getQuantifier(quantified->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Star);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)* RETURN a");
  assertNormalizedRoundTrip("MATCH (a)* RETURN a");
}

TEST(GQLParser, NodePatternPlusQuantifier) {
  auto ast = parseGraphOrThrow("MATCH (a)+ RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* quantified = getQuantifiedPathPrimary(term->factors[0]);
  ASSERT_NE(quantified, nullptr);
  ASSERT_NE(quantified->operand->as<GAST::GQLNodePattern>(), nullptr);
  const auto* quantifier = getQuantifier(quantified->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Plus);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)+ RETURN a");
  assertNormalizedRoundTrip("MATCH (a)+ RETURN a");
}

TEST(GQLParser, EdgePatternOpenRangeLowerBound) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->{2,}(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);

  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  const auto* quantifier = getQuantifier(edge->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Range);
  EXPECT_EQ(quantifier->lower, "2");
  EXPECT_EQ(quantifier->upper, "");
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->{2, }(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->{2, }(b) RETURN a");
}

TEST(GQLParser, EdgePatternOpenRangeUpperBound) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->{,5}(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);

  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  const auto* quantifier = getQuantifier(edge->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Range);
  EXPECT_EQ(quantifier->lower, "");
  EXPECT_EQ(quantifier->upper, "5");
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->{, 5}(b) RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->{, 5}(b) RETURN a");
}

TEST(GQLParser, TopLevelPathPatternUnionBuildsAlternation) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) | (c)-[f]->(d) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* alternation = getPathPatternAlternation(path->expression);
  ASSERT_NE(alternation, nullptr);
  EXPECT_EQ(alternation->kind, GAST::GQLPathPatternAlternation::Kind::Union);
  ASSERT_EQ(alternation->operands.size(), 2);
  ASSERT_NE(getPathTerm(alternation->operands[0]), nullptr);
  ASSERT_NE(getPathTerm(alternation->operands[1]), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) | (c)-[f]->(d) RETURN a");
}

TEST(GQLParser, TopLevelPathPatternAlternationBuildsStructuredAlternation) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) |+| (c)-[f]->(d) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* alternation = getPathPatternAlternation(path->expression);
  ASSERT_NE(alternation, nullptr);
  EXPECT_EQ(alternation->kind, GAST::GQLPathPatternAlternation::Kind::MultisetAlternation);
  ASSERT_EQ(alternation->operands.size(), 2);
  ASSERT_NE(getPathTerm(alternation->operands[0]), nullptr);
  ASSERT_NE(getPathTerm(alternation->operands[1]), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) |+| (c)-[f]->(d) RETURN a");
}

TEST(GQLParser, ParenthesizedPathPatternKeepsAlternationAndQuestionQuantifier) {
  auto ast = parseGraphOrThrow("MATCH ((a)-[e]->(b) | (c)-[f]->(d))? RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  const auto* alternation = getPathPatternAlternation(parenthesized->expression);
  ASSERT_NE(alternation, nullptr);
  EXPECT_EQ(alternation->kind, GAST::GQLPathPatternAlternation::Kind::Union);

  const auto* quantifier = getQuantifier(parenthesized->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Question);
  EXPECT_EQ(formatAST(*clauses), "MATCH ((a)-[e]->(b) | (c)-[f]->(d))? RETURN e");
}

TEST(GQLParser, PathPatternPrefixStaysOutsideAlternation) {
  auto ast = parseGraphOrThrow("MATCH TRAIL (a)-[e]->(b) | (c)-[f]->(d) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* prefix = getPathModePrefix(path->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Trail);

  const auto* alternation = getPathPatternAlternation(path->expression);
  ASSERT_NE(alternation, nullptr);
  ASSERT_EQ(alternation->operands.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "MATCH TRAIL (a)-[e]->(b) | (c)-[f]->(d) RETURN a");
}

TEST(GQLParser, PathPatternVariableStaysOutsideAlternation) {
  auto ast = parseGraphOrThrow("MATCH p = (a)-[e]->(b) | (c)-[f]->(d) RETURN p");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);

  const auto* variable = getExpr(path->variable);
  ASSERT_NE(variable, nullptr);
  EXPECT_EQ(variable->text, "p");
  ASSERT_NE(getPathPatternAlternation(path->expression), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH p = (a)-[e]->(b) | (c)-[f]->(d) RETURN p");
}

TEST(GQLParser, PathPatternListKeepsAlternationScopedToSinglePattern) {
  auto ast = parseGraphOrThrow("MATCH (a), (b)-[e]->(c) | (d)-[f]->(g) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_EQ(match->path_patterns.size(), 2);

  const auto* first = getPathPattern(match->path_patterns[0]);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(getPathTerm(*first), nullptr);

  const auto* second = getPathPattern(match->path_patterns[1]);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(getPathPatternAlternation(second->expression), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a), (b)-[e]->(c) | (d)-[f]->(g) RETURN a");
}

TEST(GQLParser, PathTermCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);

  auto cloned = term->clone();
  const auto* cloned_term = getPathTerm(cloned);
  ASSERT_NE(cloned_term, nullptr);
  ASSERT_EQ(cloned_term->factors.size(), 3);

  for (size_t i = 0; i < cloned_term->factors.size(); ++i) {
    EXPECT_NE(cloned_term->factors[i].get(), term->factors[i].get());
  }

  EXPECT_EQ(formatAST(*term), "(a)-[e]->(b)");
  EXPECT_EQ(formatAST(*cloned_term), "(a)-[e]->(b)");
}

TEST(GQLParser, PathPatternAlternationCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) | (c)-[f]->(d) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* alternation = getPathPatternAlternation(path->expression);
  ASSERT_NE(alternation, nullptr);
  ASSERT_EQ(alternation->operands.size(), 2);

  auto cloned = alternation->clone();
  const auto* cloned_alternation = getPathPatternAlternation(cloned);
  ASSERT_NE(cloned_alternation, nullptr);
  ASSERT_EQ(cloned_alternation->operands.size(), 2);

  for (size_t i = 0; i < cloned_alternation->operands.size(); ++i) {
    EXPECT_NE(cloned_alternation->operands[i].get(), alternation->operands[i].get());
    ASSERT_NE(getPathTerm(cloned_alternation->operands[i]), nullptr);
  }

  EXPECT_EQ(formatAST(*alternation), "(a)-[e]->(b) | (c)-[f]->(d)");
  EXPECT_EQ(formatAST(*cloned_alternation), "(a)-[e]->(b) | (c)-[f]->(d)");
}

TEST(GQLParser, PathPatternCloneKeepsChildrenOrder) {
  auto ast = parseGraphOrThrow("MATCH p = TRAIL (a)-[e]->(b) RETURN p");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  ASSERT_EQ(path->children.size(), 3);
  ASSERT_NE(getExpr(path->children[0]), nullptr);
  ASSERT_NE(path->children[1]->as<GAST::GQLPathModePrefix>(), nullptr);
  ASSERT_NE(path->children[2]->as<GAST::GQLPathTerm>(), nullptr);

  auto cloned = path->clone();
  const auto* cloned_path = getPathPattern(cloned);
  ASSERT_NE(cloned_path, nullptr);
  ASSERT_EQ(cloned_path->children.size(), 3);
  EXPECT_NE(getExpr(cloned_path->children[0]), nullptr);
  EXPECT_NE(cloned_path->children[1]->as<GAST::GQLPathModePrefix>(), nullptr);
  EXPECT_NE(cloned_path->children[2]->as<GAST::GQLPathTerm>(), nullptr);
}

TEST(GQLParser, ParenthesizedPathPatternCloneKeepsChildrenOrder) {
  auto ast = parseGraphOrThrow("MATCH (q = TRAIL (a)-[e]->(b) WHERE e)? RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  ASSERT_EQ(parenthesized->children.size(), 5);
  ASSERT_NE(getExpr(parenthesized->children[0]), nullptr);
  ASSERT_NE(parenthesized->children[1]->as<GAST::GQLPathModePrefix>(), nullptr);
  ASSERT_NE(parenthesized->children[2]->as<GAST::GQLPathTerm>(), nullptr);
  ASSERT_NE(parenthesized->children[3]->as<GAST::GQLWhereClause>(), nullptr);
  ASSERT_NE(parenthesized->children[4]->as<GAST::GQLQuantifier>(), nullptr);

  auto cloned = parenthesized->clone();
  const auto* cloned_parenthesized = getParenthesizedPathPattern(cloned);
  ASSERT_NE(cloned_parenthesized, nullptr);
  ASSERT_EQ(cloned_parenthesized->children.size(), 5);
  EXPECT_NE(getExpr(cloned_parenthesized->children[0]), nullptr);
  EXPECT_NE(cloned_parenthesized->children[1]->as<GAST::GQLPathModePrefix>(), nullptr);
  EXPECT_NE(cloned_parenthesized->children[2]->as<GAST::GQLPathTerm>(), nullptr);
  EXPECT_NE(cloned_parenthesized->children[3]->as<GAST::GQLWhereClause>(), nullptr);
  EXPECT_NE(cloned_parenthesized->children[4]->as<GAST::GQLQuantifier>(), nullptr);
}

TEST(GQLParser, ParenthesizedPathPatternSubpathVariable) {
  auto ast = parseGraphOrThrow("MATCH (p = (a)-[e]->(b)) RETURN p");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  ASSERT_NE(parenthesized->subpath_variable, nullptr);
  EXPECT_EQ(getExpr(parenthesized->subpath_variable)->text, "p");
  EXPECT_EQ(parenthesized->prefix, nullptr);
  EXPECT_EQ(parenthesized->where, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (p = (a)-[e]->(b)) RETURN p");
  assertNormalizedRoundTrip("MATCH (p = (a)-[e]->(b)) RETURN p");
}

TEST(GQLParser, ParenthesizedPathPatternPathModePrefix) {
  auto ast = parseGraphOrThrow("MATCH (TRAIL (a)-[e]->(b)) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  EXPECT_EQ(parenthesized->subpath_variable, nullptr);
  ASSERT_NE(parenthesized->prefix, nullptr);
  const auto* prefix = getPathModePrefix(parenthesized->prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Trail);
  EXPECT_EQ(parenthesized->where, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (TRAIL (a)-[e]->(b)) RETURN a");
  assertNormalizedRoundTrip("MATCH (TRAIL (a)-[e]->(b)) RETURN a");
}

TEST(GQLParser, ParenthesizedPathPatternWhereClause) {
  auto ast = parseGraphOrThrow("MATCH ((a)-[e]->(b) WHERE e IS NOT NULL) RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  EXPECT_EQ(parenthesized->subpath_variable, nullptr);
  EXPECT_EQ(parenthesized->prefix, nullptr);
  ASSERT_NE(parenthesized->where, nullptr);
  const auto* where = parenthesized->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  ASSERT_NE(getExpr(where->expression), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH ((a)-[e]->(b) WHERE (e IS NOT NULL)) RETURN a");
  assertNormalizedRoundTrip("MATCH ((a)-[e]->(b) WHERE (e IS NOT NULL)) RETURN a");
}

TEST(GQLParser, ParenthesizedPathPatternFullCloneDeep) {
  auto ast = parseGraphOrThrow("MATCH (q = TRAIL (a)-[e]->(b) WHERE e)? RETURN e");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 1);

  const auto* parenthesized = getParenthesizedPathPattern(term->factors[0]);
  ASSERT_NE(parenthesized, nullptr);
  ASSERT_NE(parenthesized->subpath_variable, nullptr);
  EXPECT_EQ(getExpr(parenthesized->subpath_variable)->text, "q");
  ASSERT_NE(parenthesized->prefix, nullptr);
  const auto* orig_prefix = getPathModePrefix(parenthesized->prefix);
  ASSERT_NE(orig_prefix, nullptr);
  EXPECT_EQ(orig_prefix->path_mode, GAST::PathMode::Trail);
  ASSERT_NE(parenthesized->where, nullptr);
  ASSERT_NE(parenthesized->quantifier, nullptr);

  String original_fmt = formatAST(*parenthesized);
  auto cloned = parenthesized->clone();
  const auto* cp = getParenthesizedPathPattern(cloned);
  ASSERT_NE(cp, nullptr);
  ASSERT_NE(cp->subpath_variable, nullptr);
  EXPECT_NE(cp->subpath_variable.get(), parenthesized->subpath_variable.get());
  ASSERT_NE(cp->prefix, nullptr);
  EXPECT_NE(cp->prefix.get(), parenthesized->prefix.get());
  const auto* cloned_prefix = getPathModePrefix(cp->prefix);
  ASSERT_NE(cloned_prefix, nullptr);
  EXPECT_EQ(cloned_prefix->path_mode, GAST::PathMode::Trail);
  ASSERT_NE(cp->where, nullptr);
  EXPECT_NE(cp->where.get(), parenthesized->where.get());
  ASSERT_NE(cp->quantifier, nullptr);
  EXPECT_NE(cp->quantifier.get(), parenthesized->quantifier.get());
  EXPECT_EQ(formatAST(*cp), original_fmt);
  EXPECT_EQ(formatAST(*clauses), "MATCH (q = TRAIL (a)-[e]->(b) WHERE e)? RETURN e");
  assertNormalizedRoundTrip("MATCH (q = TRAIL (a)-[e]->(b) WHERE e)? RETURN e");
}

TEST(GQLParser, SimplifiedPathPatternBuildsDedicatedElementTree) {
  auto ast = parseGraphOrThrow("MATCH (a)-/LIKES FOLLOWS+/->(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  EXPECT_EQ(simplified->default_direction, GAST::EdgeDirection::Right);
  EXPECT_EQ(simplified->quantifier, nullptr);

  const auto* concatenation = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(concatenation, nullptr);
  EXPECT_EQ(concatenation->kind, GAST::GQLSimplifiedPathExpr::Kind::Concatenation);
  ASSERT_EQ(concatenation->operands.size(), 2);

  const auto* first = getSimplifiedPathExpr(concatenation->operands[0]);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->kind, GAST::GQLSimplifiedPathExpr::Kind::Label);
  EXPECT_EQ(first->text, "LIKES");

  const auto* repetition = getSimplifiedPathExpr(concatenation->operands[1]);
  ASSERT_NE(repetition, nullptr);
  EXPECT_EQ(repetition->kind, GAST::GQLSimplifiedPathExpr::Kind::Repetition);

  const auto* repeated = getSimplifiedPathExpr(repetition->operand);
  const auto* quantifier = getQuantifier(repetition->quantifier);
  ASSERT_NE(repeated, nullptr);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(repeated->kind, GAST::GQLSimplifiedPathExpr::Kind::Label);
  EXPECT_EQ(repeated->text, "FOLLOWS");
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Plus);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-/LIKES FOLLOWS+/->(b) RETURN b");
}

TEST(GQLParser, SimplifiedPathPatternKeepsGroupAndUnion) {
  auto ast = parseGraphOrThrow("MATCH (a)-/(LIKES|FOLLOWS) KNOWS/->(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);

  const auto* concatenation = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(concatenation, nullptr);
  EXPECT_EQ(concatenation->kind, GAST::GQLSimplifiedPathExpr::Kind::Concatenation);
  ASSERT_EQ(concatenation->operands.size(), 2);

  const auto* group = getSimplifiedPathExpr(concatenation->operands[0]);
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(group->kind, GAST::GQLSimplifiedPathExpr::Kind::Group);

  const auto* path_union = getSimplifiedPathExpr(group->operand);
  ASSERT_NE(path_union, nullptr);
  EXPECT_EQ(path_union->kind, GAST::GQLSimplifiedPathExpr::Kind::Union);
  ASSERT_EQ(path_union->operands.size(), 2);
  EXPECT_EQ(getSimplifiedPathExpr(path_union->operands[0])->text, "LIKES");
  EXPECT_EQ(getSimplifiedPathExpr(path_union->operands[1])->text, "FOLLOWS");
  EXPECT_EQ(getSimplifiedPathExpr(concatenation->operands[1])->text, "KNOWS");
}

TEST(GQLParser, SimplifiedPathPatternKeepsDirectionOverrideAndMultisetAlternation) {
  auto ast = parseGraphOrThrow("MATCH (a)-/<LIKES|+|FOLLOWS/-(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  EXPECT_EQ(simplified->default_direction, GAST::EdgeDirection::Any);

  const auto* alternation = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(alternation, nullptr);
  EXPECT_EQ(alternation->kind, GAST::GQLSimplifiedPathExpr::Kind::MultisetAlternation);
  ASSERT_EQ(alternation->operands.size(), 2);

  const auto* override_left = getSimplifiedPathExpr(alternation->operands[0]);
  ASSERT_NE(override_left, nullptr);
  EXPECT_EQ(override_left->kind, GAST::GQLSimplifiedPathExpr::Kind::DirectionOverride);
  EXPECT_EQ(override_left->direction, GAST::EdgeDirection::Left);
  EXPECT_EQ(getSimplifiedPathExpr(override_left->operand)->text, "LIKES");
  EXPECT_EQ(getSimplifiedPathExpr(alternation->operands[1])->text, "FOLLOWS");
  EXPECT_EQ(formatAST(*simplified), "-/<LIKES |+| FOLLOWS/-");
}

TEST(GQLParser, SimplifiedPathPatternKeepsOuterQuantifier) {
  auto ast = parseGraphOrThrow("MATCH (a)-/KNOWS/->{2}(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  const auto* quantifier = getQuantifier(simplified->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Exact);
  EXPECT_EQ(quantifier->lower, "2");
  auto cloned = simplified->clone();
  EXPECT_EQ(formatAST(*simplified), "-/KNOWS/->{2}");
  EXPECT_EQ(formatAST(*cloned), "-/KNOWS/->{2}");
}

TEST(GQLParser, SimplifiedPathPatternKeepsQuestionQuantifier) {
  auto ast = parseGraphOrThrow("MATCH (a)-/KNOWS/->?(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  const auto* quantifier = getQuantifier(simplified->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Question);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-/KNOWS/->?(b) RETURN b");
}

TEST(GQLParser, SimplifiedPathPatternKeepsAnyDirectionOverride) {
  auto ast = parseGraphOrThrow("MATCH (a)-/-LIKES/-(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  EXPECT_EQ(simplified->default_direction, GAST::EdgeDirection::Any);

  const auto* override_any = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(override_any, nullptr);
  EXPECT_EQ(override_any->kind, GAST::GQLSimplifiedPathExpr::Kind::DirectionOverride);
  EXPECT_EQ(override_any->direction, GAST::EdgeDirection::Any);

  const auto* label = getSimplifiedPathExpr(override_any->operand);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->kind, GAST::GQLSimplifiedPathExpr::Kind::Label);
  EXPECT_EQ(label->text, "LIKES");
  EXPECT_EQ(formatAST(*simplified), "-/-LIKES/-");
}

TEST(GQLParser, SimplifiedPathPatternKeepsConjunction) {
  auto ast = parseGraphOrThrow("MATCH (a)-/LIKES & FOLLOWS/->(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);

  const auto* conjunction = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(conjunction, nullptr);
  EXPECT_EQ(conjunction->kind, GAST::GQLSimplifiedPathExpr::Kind::Conjunction);
  ASSERT_EQ(conjunction->operands.size(), 2);
  EXPECT_EQ(getSimplifiedPathExpr(conjunction->operands[0])->text, "LIKES");
  EXPECT_EQ(getSimplifiedPathExpr(conjunction->operands[1])->text, "FOLLOWS");
  EXPECT_EQ(formatAST(*simplified), "-/LIKES & FOLLOWS/->");
}

TEST(GQLParser, SimplifiedPathPatternKeepsUndirectedDefaulting) {
  auto ast = parseGraphOrThrow("MATCH (a)~/KNOWS/~(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  EXPECT_EQ(simplified->default_direction, GAST::EdgeDirection::Undirected);
  EXPECT_EQ(getSimplifiedPathExpr(simplified->expression)->text, "KNOWS");
  EXPECT_EQ(formatAST(*simplified), "~/KNOWS/~");
}

TEST(GQLParser, SimplifiedPathPatternKeepsUndirectedOrRightDefaulting) {
  auto ast = parseGraphOrThrow("MATCH (a)~/KNOWS/~>(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);
  EXPECT_EQ(simplified->default_direction, GAST::EdgeDirection::UndirectedOrRight);
  EXPECT_EQ(getSimplifiedPathExpr(simplified->expression)->text, "KNOWS");
  EXPECT_EQ(formatAST(*simplified), "~/KNOWS/~>");
}

TEST(GQLParser, SimplifiedPathPatternKeepsGroupRepetition) {
  auto ast = parseGraphOrThrow("MATCH (a)-/(LIKES)+/->(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);

  const auto* repetition = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(repetition, nullptr);
  EXPECT_EQ(repetition->kind, GAST::GQLSimplifiedPathExpr::Kind::Repetition);

  const auto* group = getSimplifiedPathExpr(repetition->operand);
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(group->kind, GAST::GQLSimplifiedPathExpr::Kind::Group);
  EXPECT_EQ(getSimplifiedPathExpr(group->operand)->text, "LIKES");

  const auto* quantifier = getQuantifier(repetition->quantifier);
  ASSERT_NE(quantifier, nullptr);
  EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Plus);

  auto cloned = simplified->clone();
  EXPECT_EQ(formatAST(*simplified), "-/(LIKES)+/->");
  EXPECT_EQ(formatAST(*cloned), "-/(LIKES)+/->");
}

TEST(GQLParser, SimplifiedPathPatternCloneCopiesNaryOperands) {
  auto ast = parseGraphOrThrow("MATCH (a)-/LIKES|FOLLOWS|KNOWS/->(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);

  const auto* path_union = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(path_union, nullptr);
  EXPECT_EQ(path_union->kind, GAST::GQLSimplifiedPathExpr::Kind::Union);
  ASSERT_EQ(path_union->operands.size(), 3);

  auto cloned = simplified->clone();
  const auto* cloned_simplified = cloned->as<GAST::GQLSimplifiedPathPattern>();
  ASSERT_NE(cloned_simplified, nullptr);

  const auto* cloned_union = getSimplifiedPathExpr(cloned_simplified->expression);
  ASSERT_NE(cloned_union, nullptr);
  EXPECT_EQ(cloned_union->kind, GAST::GQLSimplifiedPathExpr::Kind::Union);
  ASSERT_EQ(cloned_union->operands.size(), 3);

  for (size_t i = 0; i < cloned_union->operands.size(); ++i) {
    EXPECT_NE(cloned_union->operands[i].get(), path_union->operands[i].get());
  }

  EXPECT_EQ(formatAST(*simplified), "-/LIKES | FOLLOWS | KNOWS/->");
  EXPECT_EQ(formatAST(*cloned), "-/LIKES | FOLLOWS | KNOWS/->");
}

TEST(GQLParser, SimplifiedPathPatternKeepsQuotedLabelText) {
  auto ast = parseGraphOrThrow("MATCH (a)-/`KNOWS`/->(b) RETURN b");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);

  const auto* simplified = getSimplifiedPathPattern(term->factors[1]);
  ASSERT_NE(simplified, nullptr);

  const auto* label_expr = getSimplifiedPathExpr(simplified->expression);
  ASSERT_NE(label_expr, nullptr);
  EXPECT_EQ(label_expr->kind, GAST::GQLSimplifiedPathExpr::Kind::Label);
  EXPECT_EQ(label_expr->text, "`KNOWS`");
  EXPECT_EQ(formatAST(*simplified), "-/`KNOWS`/->");
}

TEST(GQLParser, OptionalMatchBlockKeepsStructuredOperand) {
  auto ast = parseGraphOrThrow("OPTIONAL { MATCH (a) MATCH (a)-[]->(b) } RETURN *");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* match = getMatchClause(*clauses, 0);
  ASSERT_NE(match, nullptr);
  EXPECT_TRUE(match->optional);

  const auto* block = getMatchStatementBlock(match->optional_operand_block);
  ASSERT_NE(block, nullptr);
  EXPECT_FALSE(block->parenthesized);
  ASSERT_EQ(block->matches.size(), 2);
  ASSERT_NE(block->matches[0]->as<GAST::GQLMatchClause>(), nullptr);
  ASSERT_NE(block->matches[1]->as<GAST::GQLMatchClause>(), nullptr);
  auto cloned = match->clone();
  EXPECT_EQ(formatAST(*match), "OPTIONAL { MATCH (a) MATCH (a)-[]->(b) }");
  EXPECT_EQ(formatAST(*cloned), "OPTIONAL { MATCH (a) MATCH (a)-[]->(b) }");
}

TEST(GQLParser, OptionalMatchParenthesizedBlockKeepsStructuredOperand) {
  auto ast = parseGraphOrThrow("OPTIONAL ( MATCH (a) ) RETURN *");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* match = getMatchClause(*clauses, 0);
  ASSERT_NE(match, nullptr);
  EXPECT_TRUE(match->optional);

  const auto* block = getMatchStatementBlock(match->optional_operand_block);
  ASSERT_NE(block, nullptr);
  EXPECT_TRUE(block->parenthesized);
  ASSERT_EQ(block->matches.size(), 1);
  ASSERT_NE(block->matches[0]->as<GAST::GQLMatchClause>(), nullptr);
  auto cloned = match->clone();
  EXPECT_EQ(formatAST(*match), "OPTIONAL ( MATCH (a) )");
  EXPECT_EQ(formatAST(*cloned), "OPTIONAL ( MATCH (a) )");
}

TEST(GQLParser, ExistsPredicateKeepsGraphPatternBlock) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { WALK (a)-[e]->(b) } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  EXPECT_EQ(exists->kind, GAST::GQLExpr::Kind::UnaryOp);
  EXPECT_EQ(exists->text, "EXISTS ");
  ASSERT_EQ(exists->children.size(), 1);

  const auto* block = getGraphPatternBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->match_mode, GAST::GraphMatchMode::None);
  ASSERT_EQ(block->path_patterns.size(), 1);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { WALK (a)-[e]->(b) } RETURN a");
}

TEST(GQLParser, MatchClauseKeepsKeepClause) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP TRAIL RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);

  const auto* path_prefix = getPathModePrefix(keep->path_prefix);
  ASSERT_NE(path_prefix, nullptr);
  EXPECT_EQ(path_prefix->path_mode, GAST::PathMode::Trail);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP TRAIL RETURN a");
}

TEST(GQLParser, GraphPatternBlockKeepsKeepClause) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { WALK (a)-[e]->(b) KEEP TRAIL } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  ASSERT_EQ(exists->children.size(), 1);

  const auto* block = getGraphPatternBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  const auto* keep = getKeepClause(block->keep_clause);
  ASSERT_NE(keep, nullptr);
  ASSERT_NE(getPathModePrefix(keep->path_prefix), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { WALK (a)-[e]->(b) KEEP TRAIL } RETURN a");
}

TEST(GQLParser, KeepClauseCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP TRAIL RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);

  auto cloned = keep->clone();
  const auto* cloned_keep = getKeepClause(cloned);
  ASSERT_NE(cloned_keep, nullptr);
  ASSERT_NE(cloned_keep->path_prefix, nullptr);
  EXPECT_NE(cloned_keep->path_prefix.get(), keep->path_prefix.get());
  EXPECT_EQ(formatAST(*keep), "KEEP TRAIL");
  EXPECT_EQ(formatAST(*cloned_keep), "KEEP TRAIL");
}

TEST(GQLParser, MatchClauseCloneKeepsChildrenOrder) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP TRAIL WHERE a RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_EQ(match->children.size(), 3);
  ASSERT_NE(match->children[0]->as<GAST::GQLPathPattern>(), nullptr);
  ASSERT_NE(match->children[1]->as<GAST::GQLKeepClause>(), nullptr);
  ASSERT_NE(match->children[2]->as<GAST::GQLWhereClause>(), nullptr);

  auto cloned = match->clone();
  const auto* cloned_match = cloned->as<GAST::GQLMatchClause>();
  ASSERT_NE(cloned_match, nullptr);
  ASSERT_EQ(cloned_match->children.size(), 3);
  EXPECT_NE(cloned_match->children[0]->as<GAST::GQLPathPattern>(), nullptr);
  EXPECT_NE(cloned_match->children[1]->as<GAST::GQLKeepClause>(), nullptr);
  EXPECT_NE(cloned_match->children[2]->as<GAST::GQLWhereClause>(), nullptr);
}

TEST(GQLParser, GraphPatternBlockCloneKeepsChildrenOrder) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { WALK (a)-[e]->(b) KEEP TRAIL WHERE e } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  ASSERT_EQ(exists->children.size(), 1);

  const auto* block = getGraphPatternBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->children.size(), 3);
  ASSERT_NE(block->children[0]->as<GAST::GQLPathPattern>(), nullptr);
  ASSERT_NE(block->children[1]->as<GAST::GQLKeepClause>(), nullptr);
  ASSERT_NE(block->children[2]->as<GAST::GQLWhereClause>(), nullptr);

  auto cloned = block->clone();
  const auto* cloned_block = cloned->as<GAST::GQLGraphPatternBlock>();
  ASSERT_NE(cloned_block, nullptr);
  ASSERT_EQ(cloned_block->children.size(), 3);
  EXPECT_NE(cloned_block->children[0]->as<GAST::GQLPathPattern>(), nullptr);
  EXPECT_NE(cloned_block->children[1]->as<GAST::GQLKeepClause>(), nullptr);
  EXPECT_NE(cloned_block->children[2]->as<GAST::GQLWhereClause>(), nullptr);
}

TEST(GQLParser, KeepAnyCountedPathsPrefix) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP ANY 2 PATHS RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);

  const auto* prefix = getPathSearchPrefix(keep->path_prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::Any);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Paths);
  EXPECT_TRUE(prefix->use_paths_keyword);
  ASSERT_NE(prefix->count, nullptr);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->text, "2");
  EXPECT_EQ(count->kind, GAST::CountSpecKind::Integer);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP ANY 2 PATHS RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->(b) KEEP ANY 2 PATHS RETURN a");
}

TEST(GQLParser, KeepAllShortestTrailPathsPrefix) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP ALL SHORTEST TRAIL PATHS RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);

  const auto* prefix = getPathSearchPrefix(keep->path_prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::AllShortest);
  EXPECT_EQ(prefix->path_mode, GAST::PathMode::Trail);
  EXPECT_TRUE(prefix->has_path_keyword);
  EXPECT_TRUE(prefix->use_paths_keyword);
  EXPECT_EQ(prefix->count, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP ALL SHORTEST TRAIL PATHS RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->(b) KEEP ALL SHORTEST TRAIL PATHS RETURN a");
}

TEST(GQLParser, KeepShortestDynamicParamGroups) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) KEEP SHORTEST $n GROUPS RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);

  const auto* prefix = getPathSearchPrefix(keep->path_prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::CountedShortestGroup);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Groups);
  EXPECT_TRUE(prefix->use_groups_keyword);
  ASSERT_NE(prefix->count, nullptr);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->text, "$n");
  EXPECT_EQ(count->kind, GAST::CountSpecKind::DynamicParameter);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) KEEP SHORTEST $n GROUPS RETURN a");
  assertNormalizedRoundTrip("MATCH (a)-[e]->(b) KEEP SHORTEST $n GROUPS RETURN a");
}

TEST(GQLParser, KeepSearchPrefixInExistsBlock) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { (a)-[e]->(b) KEEP ANY 3 PATHS } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  ASSERT_EQ(exists->children.size(), 1);

  const auto* block = getGraphPatternBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  ASSERT_NE(block->keep_clause, nullptr);

  const auto* keep = getKeepClause(block->keep_clause);
  ASSERT_NE(keep, nullptr);
  const auto* prefix = getPathSearchPrefix(keep->path_prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::Any);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Paths);
  ASSERT_NE(prefix->count, nullptr);
  EXPECT_EQ(getCountSpec(prefix->count)->text, "3");
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { (a)-[e]->(b) KEEP ANY 3 PATHS } RETURN a");
}

TEST(GQLParser, ExistsPredicateKeepsMatchStatementBlock) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { MATCH (a)-[e]->(b) } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  ASSERT_EQ(exists->children.size(), 1);

  const auto* block = getMatchStatementBlock(exists->children[0]);
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->matches.size(), 1);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { MATCH (a)-[e]->(b) } RETURN a");
}

TEST(GQLParser, ExistsPredicateKeepsNestedQuery) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE EXISTS { RETURN a } RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* exists = getExpr(where->expression);
  ASSERT_NE(exists, nullptr);
  ASSERT_EQ(exists->children.size(), 1);
  ASSERT_NE(getSubquery(exists->children[0]), nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE EXISTS { RETURN a } RETURN a");
}

TEST(GQLParser, ValueFunctionsBuildStructuredExpressions) {
  auto ast = parseGraphOrThrow("RETURN ABS(-1), CHAR_LENGTH('x'), CARDINALITY([1, 2])");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* return_clause = getReturnClause(*clauses, 0);
  ASSERT_NE(return_clause, nullptr);
  ASSERT_EQ(return_clause->items.size(), 3);

  const auto* abs_expr = getExpr(return_clause->items[0]);
  const auto* length_expr = getExpr(return_clause->items[1]);
  const auto* cardinality_expr = getExpr(return_clause->items[2]);
  ASSERT_NE(abs_expr, nullptr);
  ASSERT_NE(length_expr, nullptr);
  ASSERT_NE(cardinality_expr, nullptr);
  EXPECT_EQ(abs_expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(abs_expr->text, "ABS");
  EXPECT_EQ(length_expr->text, "CHAR_LENGTH");
  EXPECT_EQ(cardinality_expr->text, "CARDINALITY");
  ASSERT_EQ(cardinality_expr->children.size(), 1);
  ASSERT_NE(getListConstructor(cardinality_expr->children[0]), nullptr);
}

TEST(GQLParser, AggregateAndConstructorsBuildStructuredNodes) {
  auto ast = parseGraphOrThrow("RETURN COUNT(*), [1, 2], RECORD {name: 1}");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* return_clause = getReturnClause(*clauses, 0);
  ASSERT_NE(return_clause, nullptr);
  ASSERT_EQ(return_clause->items.size(), 3);

  const auto* count = getExpr(return_clause->items[0]);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(count->text, "COUNT");
  ASSERT_EQ(count->children.size(), 1);
  EXPECT_EQ(getExpr(count->children[0])->text, "*");

  const auto* list = getListConstructor(return_clause->items[1]);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->items.size(), 2);

  const auto* record = getRecordConstructor(return_clause->items[2]);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->explicit_record_keyword);
  ASSERT_EQ(record->fields.size(), 1);
  EXPECT_EQ(formatAST(*clauses), "RETURN COUNT(*), [1, 2], RECORD {name: 1}");
}

TEST(GQLParser, AggregateFunctionsKeepSetQuantifierStructure) {
  auto ast = parseGraphOrThrow("RETURN SUM(DISTINCT a), COUNT(ALL b), PERCENTILE_CONT(DISTINCT a, 0.5)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 1);

  const auto* return_clause = getReturnClause(*clauses, 0);
  ASSERT_NE(return_clause, nullptr);
  ASSERT_EQ(return_clause->items.size(), 3);

  const auto* sum = getExpr(return_clause->items[0]);
  const auto* count = getExpr(return_clause->items[1]);
  const auto* percentile = getExpr(return_clause->items[2]);
  ASSERT_NE(sum, nullptr);
  ASSERT_NE(count, nullptr);
  ASSERT_NE(percentile, nullptr);

  EXPECT_EQ(sum->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(sum->set_quantifier, GAST::GQLExpr::SetQuantifier::Distinct);
  EXPECT_EQ(count->set_quantifier, GAST::GQLExpr::SetQuantifier::All);
  EXPECT_EQ(percentile->text, "PERCENTILE_CONT");
  EXPECT_EQ(percentile->set_quantifier, GAST::GQLExpr::SetQuantifier::Distinct);
  ASSERT_EQ(percentile->children.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "RETURN SUM(DISTINCT a), COUNT(ALL b), PERCENTILE_CONT(DISTINCT a, 0.5)");
}

TEST(GQLParser, ValuePrimaryKeepsDynamicAndSpecialValuesStructured) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN $x, SESSION_USER, TRUE, NULL, ELEMENT_ID(a)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_EQ(clauses->clauses.size(), 2);

  const auto* return_clause = getReturnClause(*clauses);
  ASSERT_NE(return_clause, nullptr);
  ASSERT_EQ(return_clause->items.size(), 5);

  const auto* parameter = getExpr(return_clause->items[0]);
  const auto* session_user = getExpr(return_clause->items[1]);
  const auto* truth_value = getExpr(return_clause->items[2]);
  const auto* null_value = getExpr(return_clause->items[3]);
  const auto* element_id = getExpr(return_clause->items[4]);
  ASSERT_NE(parameter, nullptr);
  ASSERT_NE(session_user, nullptr);
  ASSERT_NE(truth_value, nullptr);
  ASSERT_NE(null_value, nullptr);
  ASSERT_NE(element_id, nullptr);

  EXPECT_EQ(parameter->kind, GAST::GQLExpr::Kind::DynamicParameter);
  EXPECT_EQ(parameter->text, "$x");
  EXPECT_EQ(session_user->kind, GAST::GQLExpr::Kind::SpecialValue);
  EXPECT_EQ(session_user->text, "SESSION_USER");
  EXPECT_EQ(truth_value->kind, GAST::GQLExpr::Kind::SpecialValue);
  EXPECT_EQ(truth_value->text, "TRUE");
  EXPECT_EQ(null_value->kind, GAST::GQLExpr::Kind::SpecialValue);
  EXPECT_EQ(null_value->text, "NULL");
  EXPECT_EQ(element_id->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(element_id->text, "ELEMENT_ID");
  ASSERT_EQ(element_id->children.size(), 1);
  EXPECT_EQ(getExpr(element_id->children[0])->text, "a");
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN $x, SESSION_USER, TRUE, NULL, ELEMENT_ID(a)");
}

TEST(GQLParser, CharacterStringLiteralPreservesText) {
  auto ast = parseGraphOrThrow("RETURN 'hello world'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(expr->text, "'hello world'");
}

TEST(GQLParser, ByteStringLiteralPreservesText) {
  auto ast = parseGraphOrThrow("RETURN X'48454C4C4F'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(expr->text, "X'48454C4C4F'");
}

TEST(GQLParser, FormatNodePattern) {
  auto node = make_intrusive<GAST::GQLNodePattern>();
  node->variable = GAST::GQLExpr::identifier("n");
  node->label_expression = GAST::GQLLabelExpression::name("Person");
  attachChildIfPresent(*node, node->variable);
  attachChildIfPresent(*node, node->label_expression);

  EXPECT_EQ(formatAST(*node), "(n:Person)");
}

TEST(GQLParser, FormatEdgePatternWithQuantifier) {
  auto edge = make_intrusive<GAST::GQLEdgePattern>(GAST::EdgeDirection::Right);
  edge->variable = GAST::GQLExpr::identifier("e");
  edge->label_expression = GAST::GQLLabelExpression::name("KNOWS");
  edge->quantifier = GAST::Ptr(make_intrusive<GAST::GQLQuantifier>(GAST::GQLQuantifier::Kind::Plus));
  attachChildIfPresent(*edge, edge->variable);
  attachChildIfPresent(*edge, edge->label_expression);
  attachChildIfPresent(*edge, edge->quantifier);

  EXPECT_EQ(formatAST(*edge), "-[e:KNOWS]->+");
}

TEST(GQLParser, CloneEdgeWithQuantifier) {
  auto edge = make_intrusive<GAST::GQLEdgePattern>(GAST::EdgeDirection::Right);
  edge->variable = GAST::GQLExpr::identifier("e");
  edge->label_expression = GAST::GQLLabelExpression::name("KNOWS");
  edge->quantifier = GAST::Ptr(make_intrusive<GAST::GQLQuantifier>(GAST::GQLQuantifier::Kind::Range, "1", "5"));
  attachChildIfPresent(*edge, edge->variable);
  attachChildIfPresent(*edge, edge->label_expression);
  attachChildIfPresent(*edge, edge->quantifier);

  auto cloned = edge->clone();
  const auto* cloned_edge = cloned->as<GAST::GQLEdgePattern>();
  ASSERT_NE(cloned_edge, nullptr);
  EXPECT_EQ(cloned_edge->direction, GAST::EdgeDirection::Right);
  EXPECT_NE(cloned_edge->variable.get(), edge->variable.get());
  EXPECT_NE(cloned_edge->quantifier.get(), edge->quantifier.get());
  EXPECT_EQ(formatAST(*cloned_edge), "-[e:KNOWS]->{1, 5}");
}

TEST(GQLParser, InvalidSyntaxThrowsException) {
  try {
    (void)parseGraphOrThrow("MATCH INVALID SYNTAX !!!");
    FAIL() << "Expected `DB::Exception`";
  } catch (const DB::Exception& e) {
    EXPECT_FALSE(e.message().empty());
  }
}

// --- Phase 5: Expression completion ---

TEST(GQLParser, CaseAbbreviationNullif) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN NULLIF(a.x, 0)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "NULLIF");
  ASSERT_EQ(expr->children.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "RETURN NULLIF(a.x, 0)");
}

TEST(GQLParser, CaseAbbreviationCoalesce) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN COALESCE(a.x, a.y, 0)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "COALESCE");
  ASSERT_EQ(expr->children.size(), 3);
  EXPECT_EQ(formatAST(*clauses), "RETURN COALESCE(a.x, a.y, 0)");
}

TEST(GQLParser, CaseAbbreviationCoalesceColumnName) {
  auto ast = parseGraphOrThrow("RETURN COALESCE(NULL, 1, 2)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses, 0);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "COALESCE");
  ASSERT_EQ(expr->children.size(), 3);
  EXPECT_EQ(expr->getColumnName(), "COALESCE(NULL, 1, 2)");
  EXPECT_EQ(formatAST(*clauses), "RETURN COALESCE(NULL, 1, 2)");
}

TEST(GQLParser, SearchedCaseExpression) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE WHEN a.x > 0 THEN 1 WHEN a.x < 0 THEN -1 ELSE 0 END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  EXPECT_EQ(case_expr->form, GAST::GQLCaseExpr::Form::Searched);
  EXPECT_EQ(case_expr->operand, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 2);
  ASSERT_EQ(case_expr->then_results.size(), 2);
  EXPECT_NE(case_expr->else_result, nullptr);
  EXPECT_EQ(formatAST(*case_expr), "CASE WHEN (a.x > 0) THEN 1 WHEN (a.x < 0) THEN -1 ELSE 0 END");
}

TEST(GQLParser, SimpleCaseExpression) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.status WHEN 1 THEN 'active' WHEN 2 THEN 'inactive' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  EXPECT_EQ(case_expr->form, GAST::GQLCaseExpr::Form::Simple);
  EXPECT_NE(case_expr->operand, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 2);
  ASSERT_EQ(case_expr->then_results.size(), 2);
  EXPECT_EQ(case_expr->else_result, nullptr);
}

TEST(GQLParser, CastSpecification) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CAST(a.score AS INT32)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::Cast);
  ASSERT_EQ(expr->children.size(), 2);
  const auto* cast_type = expr->children[1]->as<GAST::GQLTypeExpression>();
  ASSERT_NE(cast_type, nullptr);
  EXPECT_EQ(cast_type->name, "INT32");
  EXPECT_EQ(formatAST(*clauses), "RETURN CAST(a.score AS INT32)");
}

TEST(GQLParser, CastNullOperand) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CAST(NULL AS STRING)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::Cast);
  ASSERT_EQ(expr->children.size(), 2);
  const auto* cast_type = expr->children[1]->as<GAST::GQLTypeExpression>();
  ASSERT_NE(cast_type, nullptr);
  EXPECT_EQ(cast_type->name, "STRING");
  const auto* null_operand = getExpr(expr->children[0]);
  ASSERT_NE(null_operand, nullptr);
  EXPECT_EQ(null_operand->kind, GAST::GQLExpr::Kind::SpecialValue);
  EXPECT_EQ(null_operand->text, "NULL");
  EXPECT_EQ(formatAST(*expr), "CAST(NULL AS STRING)");
}

TEST(GQLParser, NumericFloorCeiling) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN FLOOR(a.x), CEILING(a.y), CEIL(a.z)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 3);

  const auto* floor_expr = getExpr(ret->items[0]);
  const auto* ceiling_expr = getExpr(ret->items[1]);
  const auto* ceil_expr = getExpr(ret->items[2]);
  EXPECT_EQ(floor_expr->text, "FLOOR");
  EXPECT_EQ(ceiling_expr->text, "CEILING");
  EXPECT_EQ(ceil_expr->text, "CEIL");
  EXPECT_EQ(formatAST(*clauses), "RETURN FLOOR(a.x), CEILING(a.y), CEIL(a.z)");
}

TEST(GQLParser, NumericModPowerSqrt) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN MOD(a.x, 3), POWER(a.y, 2), SQRT(a.z)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 3);

  const auto* mod_expr = getExpr(ret->items[0]);
  const auto* pow_expr = getExpr(ret->items[1]);
  const auto* sqrt_expr = getExpr(ret->items[2]);
  EXPECT_EQ(mod_expr->text, "MOD");
  ASSERT_EQ(mod_expr->children.size(), 2);
  EXPECT_EQ(pow_expr->text, "POWER");
  ASSERT_EQ(pow_expr->children.size(), 2);
  EXPECT_EQ(sqrt_expr->text, "SQRT");
  ASSERT_EQ(sqrt_expr->children.size(), 1);
}

TEST(GQLParser, TrigonometricFunctions) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN SIN(a.x), COS(a.x), TAN(a.x)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 3);

  EXPECT_EQ(getExpr(ret->items[0])->text, "SIN");
  EXPECT_EQ(getExpr(ret->items[1])->text, "COS");
  EXPECT_EQ(getExpr(ret->items[2])->text, "TAN");
}

TEST(GQLParser, LogarithmicFunctions) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LOG(2, a.x), LOG10(a.x), LN(a.x), EXP(a.x)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 4);

  const auto* log_expr = getExpr(ret->items[0]);
  EXPECT_EQ(log_expr->text, "LOG");
  ASSERT_EQ(log_expr->children.size(), 2);
  EXPECT_EQ(getExpr(ret->items[1])->text, "LOG10");
  EXPECT_EQ(getExpr(ret->items[2])->text, "LN");
  EXPECT_EQ(getExpr(ret->items[3])->text, "EXP");
}

TEST(GQLParser, StringFunctionsUpperLower) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN UPPER(a.name), LOWER(a.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 2);

  EXPECT_EQ(getExpr(ret->items[0])->text, "UPPER");
  EXPECT_EQ(getExpr(ret->items[1])->text, "LOWER");
  EXPECT_EQ(formatAST(*clauses), "RETURN UPPER(a.name), LOWER(a.name)");
}

TEST(GQLParser, StringFunctionsLeftRight) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LEFT(a.name, 3), RIGHT(a.name, 5)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 2);

  const auto* left_expr = getExpr(ret->items[0]);
  const auto* right_expr = getExpr(ret->items[1]);
  EXPECT_EQ(left_expr->text, "LEFT");
  ASSERT_EQ(left_expr->children.size(), 2);
  EXPECT_EQ(right_expr->text, "RIGHT");
  ASSERT_EQ(right_expr->children.size(), 2);
}

TEST(GQLParser, StringFunctionsTrim) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN BTRIM(a.name), LTRIM(a.name, ' '), RTRIM(a.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 3);

  EXPECT_EQ(getExpr(ret->items[0])->text, "BTRIM");
  EXPECT_EQ(getExpr(ret->items[1])->text, "LTRIM");
  ASSERT_EQ(getExpr(ret->items[1])->children.size(), 2);
  EXPECT_EQ(getExpr(ret->items[2])->text, "RTRIM");
}

TEST(GQLParser, SearchedCaseCloneChildrenOrder) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE WHEN a.x > 0 THEN 1 WHEN a.x < 0 THEN -1 ELSE 0 END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);

  const auto* original = getCaseExpr(ret->items[0]);
  ASSERT_NE(original, nullptr);

  auto cloned_ast = original->clone();
  const auto* cloned = cloned_ast->as<GAST::GQLCaseExpr>();
  ASSERT_NE(cloned, nullptr);
  EXPECT_NE(cloned, original);
  EXPECT_EQ(cloned->form, GAST::GQLCaseExpr::Form::Searched);
  EXPECT_EQ(cloned->operand, nullptr);
  ASSERT_EQ(cloned->when_operands.size(), 2);
  ASSERT_EQ(cloned->then_results.size(), 2);
  EXPECT_NE(cloned->else_result, nullptr);

  // children order: when1, then1, when2, then2, else
  ASSERT_EQ(cloned->children.size(), 5);
  EXPECT_EQ(cloned->children[0].get(), cloned->when_operands[0].get());
  EXPECT_EQ(cloned->children[1].get(), cloned->then_results[0].get());
  EXPECT_EQ(cloned->children[2].get(), cloned->when_operands[1].get());
  EXPECT_EQ(cloned->children[3].get(), cloned->then_results[1].get());
  EXPECT_EQ(cloned->children[4].get(), cloned->else_result.get());

  EXPECT_NE(cloned->when_operands[0].get(), original->when_operands[0].get());
  EXPECT_EQ(formatAST(*cloned), formatAST(*original));
}

TEST(GQLParser, SimpleCaseCloneChildrenOrder) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.status WHEN 1 THEN 'active' WHEN 2 THEN 'inactive' ELSE 'unknown' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);

  const auto* original = getCaseExpr(ret->items[0]);
  ASSERT_NE(original, nullptr);

  auto cloned_ast = original->clone();
  const auto* cloned = cloned_ast->as<GAST::GQLCaseExpr>();
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(cloned->form, GAST::GQLCaseExpr::Form::Simple);
  EXPECT_NE(cloned->operand, nullptr);
  ASSERT_EQ(cloned->when_operands.size(), 2);
  ASSERT_EQ(cloned->then_results.size(), 2);
  EXPECT_NE(cloned->else_result, nullptr);

  // children order: operand, when1, then1, when2, then2, else
  ASSERT_EQ(cloned->children.size(), 6);
  EXPECT_EQ(cloned->children[0].get(), cloned->operand.get());
  EXPECT_EQ(cloned->children[1].get(), cloned->when_operands[0].get());
  EXPECT_EQ(cloned->children[2].get(), cloned->then_results[0].get());
  EXPECT_EQ(cloned->children[3].get(), cloned->when_operands[1].get());
  EXPECT_EQ(cloned->children[4].get(), cloned->then_results[1].get());
  EXPECT_EQ(cloned->children[5].get(), cloned->else_result.get());

  EXPECT_NE(cloned->operand.get(), original->operand.get());
  EXPECT_EQ(formatAST(*cloned), formatAST(*original));
}

TEST(GQLParser, CastCloneRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CAST(a.x AS INT64)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);

  auto original = ret->items[0];
  auto cloned = original->clone();
  EXPECT_NE(cloned.get(), original.get());
  EXPECT_EQ(formatAST(*cloned), "CAST(a.x AS INT64)");
}

TEST(GQLParser, InsertNodeOnly) {
  auto ast = parseDMLOrThrow("INSERT (n:Person {name: 'Alice'})");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_GE(query->clauses.size(), 1);

  const auto* insert_clause = query->clauses[0]->as<GAST::GQLInsertClause>();
  ASSERT_NE(insert_clause, nullptr);
  EXPECT_EQ(insert_clause->path_patterns.size(), 1);
  EXPECT_TRUE(formatAST(*insert_clause).find("INSERT") != String::npos);
}

TEST(GQLParser, InsertNodeEdgeChain) {
  auto ast = parseDMLOrThrow("INSERT (n:Person {name: 'Alice'})-[e:KNOWS]->(m:Person {name: 'Bob'})");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* insert_clause = query->clauses[0]->as<GAST::GQLInsertClause>();
  ASSERT_NE(insert_clause, nullptr);
  EXPECT_EQ(insert_clause->path_patterns.size(), 1);

  const auto* path = insert_clause->path_patterns[0]->as<GAST::GQLInsertPathPattern>();
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->elements.size(), 3);
}

TEST(GQLParser, InsertMultiplePathPatterns) {
  auto ast = parseDMLOrThrow("INSERT (a:Person {name: 'A'}), (b:Person {name: 'B'})");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* insert_clause = query->clauses[0]->as<GAST::GQLInsertClause>();
  ASSERT_NE(insert_clause, nullptr);
  EXPECT_EQ(insert_clause->path_patterns.size(), 2);
}

TEST(GQLParser, SetPropertyItem) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) SET n.age = 30 RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_GE(query->clauses.size(), 3);

  const auto* set_clause = query->clauses[1]->as<GAST::GQLSetClause>();
  ASSERT_NE(set_clause, nullptr);
  ASSERT_EQ(set_clause->items.size(), 1);

  const auto* item = set_clause->items[0]->as<GAST::GQLSetItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLSetItem::Kind::Property);
  EXPECT_EQ(item->variable, "n");
  EXPECT_EQ(item->property_or_label, "age");
}

TEST(GQLParser, SetAllProperties) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) SET n = {name: 'Bob', age: 25} RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* set_clause = query->clauses[1]->as<GAST::GQLSetClause>();
  ASSERT_NE(set_clause, nullptr);
  ASSERT_EQ(set_clause->items.size(), 1);

  const auto* item = set_clause->items[0]->as<GAST::GQLSetItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLSetItem::Kind::AllProperties);
  EXPECT_EQ(item->variable, "n");

  const auto* prop_map = item->value->as<GAST::GQLPropertyMap>();
  ASSERT_NE(prop_map, nullptr);
  EXPECT_EQ(prop_map->items.size(), 2);
}

TEST(GQLParser, SetLabelItem) {
  auto ast = parseDMLOrThrow("MATCH (n) SET n IS Employee RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* set_clause = query->clauses[1]->as<GAST::GQLSetClause>();
  ASSERT_NE(set_clause, nullptr);

  const auto* item = set_clause->items[0]->as<GAST::GQLSetItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLSetItem::Kind::Label);
  EXPECT_EQ(item->variable, "n");
  EXPECT_EQ(item->property_or_label, "Employee");
  EXPECT_TRUE(item->use_is);
}

TEST(GQLParser, SetMultipleItems) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) SET n.age = 30, n.name = 'Bob' RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* set_clause = query->clauses[1]->as<GAST::GQLSetClause>();
  ASSERT_NE(set_clause, nullptr);
  EXPECT_EQ(set_clause->items.size(), 2);
}

TEST(GQLParser, RemovePropertyItem) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) REMOVE n.age RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* remove_clause = query->clauses[1]->as<GAST::GQLRemoveClause>();
  ASSERT_NE(remove_clause, nullptr);
  ASSERT_EQ(remove_clause->items.size(), 1);

  const auto* item = remove_clause->items[0]->as<GAST::GQLRemoveItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLRemoveItem::Kind::Property);
  EXPECT_EQ(item->variable, "n");
  EXPECT_EQ(item->property_or_label, "age");
}

TEST(GQLParser, RemoveLabelItem) {
  auto ast = parseDMLOrThrow("MATCH (n) REMOVE n IS Employee RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* remove_clause = query->clauses[1]->as<GAST::GQLRemoveClause>();
  ASSERT_NE(remove_clause, nullptr);

  const auto* item = remove_clause->items[0]->as<GAST::GQLRemoveItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLRemoveItem::Kind::Label);
  EXPECT_TRUE(item->use_is);
}

TEST(GQLParser, DeleteStatement) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) DELETE n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_GE(query->clauses.size(), 2);

  const auto* delete_clause = query->clauses[1]->as<GAST::GQLDeleteClause>();
  ASSERT_NE(delete_clause, nullptr);
  EXPECT_EQ(delete_clause->detach_mode, GAST::GQLDeleteClause::DetachMode::None);
  EXPECT_EQ(delete_clause->items.size(), 1);
}

TEST(GQLParser, DetachDeleteStatement) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) DETACH DELETE n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* delete_clause = query->clauses[1]->as<GAST::GQLDeleteClause>();
  ASSERT_NE(delete_clause, nullptr);
  EXPECT_EQ(delete_clause->detach_mode, GAST::GQLDeleteClause::DetachMode::Detach);
}

TEST(GQLParser, NodetachDeleteStatement) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) NODETACH DELETE n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* delete_clause = query->clauses[1]->as<GAST::GQLDeleteClause>();
  ASSERT_NE(delete_clause, nullptr);
  EXPECT_EQ(delete_clause->detach_mode, GAST::GQLDeleteClause::DetachMode::NoDetach);
}

TEST(GQLParser, DeleteMultipleItems) {
  auto ast = parseDMLOrThrow("MATCH (n)-[e]->(m) DELETE n, e, m");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* delete_clause = query->clauses[1]->as<GAST::GQLDeleteClause>();
  ASSERT_NE(delete_clause, nullptr);
  EXPECT_EQ(delete_clause->items.size(), 3);
}

TEST(GQLParser, MatchInsertReturn) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) INSERT (m:Employee {name: n.name})-[e:WORKS_AT]->(c:Company) RETURN m");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_GE(query->clauses.size(), 3);

  EXPECT_NE(query->clauses[0]->as<GAST::GQLMatchClause>(), nullptr);
  EXPECT_NE(query->clauses[1]->as<GAST::GQLInsertClause>(), nullptr);
  EXPECT_NE(query->clauses[2]->as<GAST::GQLReturnClause>(), nullptr);
}

TEST(GQLParser, InsertClauseFormatRoundTrip) {
  auto ast = parseDMLOrThrow("INSERT (n:Person {name: 'Alice'})");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* insert_clause = query->clauses[0]->as<GAST::GQLInsertClause>();
  ASSERT_NE(insert_clause, nullptr);

  String formatted = formatAST(*insert_clause);
  EXPECT_TRUE(formatted.find("INSERT") != String::npos);
  EXPECT_TRUE(formatted.find("Person") != String::npos);
}

TEST(GQLParser, SetClauseFormatRoundTrip) {
  auto ast = parseDMLOrThrow("MATCH (n) SET n.x = 1 RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* set_clause = query->clauses[1]->as<GAST::GQLSetClause>();
  ASSERT_NE(set_clause, nullptr);

  String formatted = formatAST(*set_clause);
  EXPECT_EQ(formatted, "SET n.x = 1");
}

TEST(GQLParser, RemoveClauseFormatRoundTrip) {
  auto ast = parseDMLOrThrow("MATCH (n) REMOVE n.x RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* remove_clause = query->clauses[1]->as<GAST::GQLRemoveClause>();
  ASSERT_NE(remove_clause, nullptr);

  String formatted = formatAST(*remove_clause);
  EXPECT_EQ(formatted, "REMOVE n.x");
}

TEST(GQLParser, DeleteClauseFormatRoundTrip) {
  auto ast = parseDMLOrThrow("MATCH (n) DETACH DELETE n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* delete_clause = query->clauses[1]->as<GAST::GQLDeleteClause>();
  ASSERT_NE(delete_clause, nullptr);

  String formatted = formatAST(*delete_clause);
  EXPECT_EQ(formatted, "DETACH DELETE n");
}

TEST(GQLParser, InsertCloneRoundTrip) {
  auto ast = parseDMLOrThrow("INSERT (n:Person {name: 'Alice'})-[e:KNOWS]->(m:Person)");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  auto original = query->clauses[0];
  auto cloned = original->clone();
  EXPECT_NE(cloned.get(), original.get());
  EXPECT_EQ(formatAST(*cloned), formatAST(*original));
}

TEST(GQLParser, SetCloneRoundTrip) {
  auto ast = parseDMLOrThrow("MATCH (n) SET n.age = 30, n IS Employee RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  auto original = query->clauses[1];
  auto cloned = original->clone();
  EXPECT_NE(cloned.get(), original.get());
  EXPECT_EQ(formatAST(*cloned), formatAST(*original));
}

TEST(GQLParser, InsertLeftEdge) {
  auto ast = parseDMLOrThrow("INSERT (n:Person)<-[e:KNOWS]-(m:Person)");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* insert_clause = query->clauses[0]->as<GAST::GQLInsertClause>();
  ASSERT_NE(insert_clause, nullptr);
  const auto* path = insert_clause->path_patterns[0]->as<GAST::GQLInsertPathPattern>();
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->elements.size(), 3);

  const auto* edge = path->elements[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->direction, GAST::EdgeDirection::Left);
}

TEST(GQLParser, InsertMultiLabel) {
  auto ast = parseDMLOrThrow("INSERT (n:Person&Employee {name: 'Alice'})");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);

  const auto* insert_clause = query->clauses[0]->as<GAST::GQLInsertClause>();
  ASSERT_NE(insert_clause, nullptr);
  const auto* path = insert_clause->path_patterns[0]->as<GAST::GQLInsertPathPattern>();
  ASSERT_NE(path, nullptr);

  const auto* node = path->elements[0]->as<GAST::GQLNodePattern>();
  ASSERT_NE(node, nullptr);
  ASSERT_NE(node->label_expression, nullptr);

  const auto* label = node->label_expression->as<GAST::GQLLabelExpression>();
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->kind, GAST::GQLLabelExpression::Kind::Conjunction);
}

// --- DML CALL via direct entry ---

TEST(GQLParser, DmlCallProcedure) {
  auto ast = parseDMLOrThrow("MATCH (n:Person) CALL myProc(n.name) RETURN n");
  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  EXPECT_GE(query->clauses.size(), 3);

  const auto* call = query->clauses[1]->as<GAST::GQLCallNamedClause>();
  ASSERT_NE(call, nullptr);
  EXPECT_NE(call->procedure, nullptr);
  EXPECT_EQ(call->arguments.size(), 1);
}

// --- Value function coverage tests ---

TEST(GQLParser, DatetimeCurrentDateNoArgs) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN CURRENT_DATE");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN CURRENT_DATE");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "CURRENT_DATE");
  EXPECT_TRUE(expr->bare_keyword);
  EXPECT_TRUE(expr->children.empty());
}

TEST(GQLParser, DatetimeCurrentTimeNoArgs) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN CURRENT_TIME");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN CURRENT_TIME");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "CURRENT_TIME");
  EXPECT_TRUE(expr->bare_keyword);
  EXPECT_TRUE(expr->children.empty());
}

TEST(GQLParser, DatetimeDateFunctionWithString) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DATE('2024-01-15')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN DATE('2024-01-15')");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "DATE");
  EXPECT_FALSE(expr->bare_keyword);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* arg = expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(arg->text, "'2024-01-15'");
}

TEST(GQLParser, DatetimeCurrentTimestampNoArgs) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN CURRENT_TIMESTAMP");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN CURRENT_TIMESTAMP");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "CURRENT_TIMESTAMP");
  EXPECT_TRUE(expr->bare_keyword);
  EXPECT_TRUE(expr->children.empty());
}

TEST(GQLParser, DatetimeZonedDatetimeWithString) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN ZONED_DATETIME('2024-01-15T10:30:00Z')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN ZONED_DATETIME('2024-01-15T10:30:00Z')");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "ZONED_DATETIME");
  EXPECT_FALSE(expr->bare_keyword);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* arg = expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(arg->text, "'2024-01-15T10:30:00Z'");
}

TEST(GQLParser, DatetimeZonedTimeWithString) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN ZONED_TIME('10:30:00+02:00')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN ZONED_TIME('10:30:00+02:00')");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "ZONED_TIME");
  EXPECT_FALSE(expr->bare_keyword);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* arg = expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(arg->text, "'10:30:00+02:00'");
}

TEST(GQLParser, DatetimeLocalTimeWithString) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN LOCAL_TIME('10:30:00')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN LOCAL_TIME('10:30:00')");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "LOCAL_TIME");
  EXPECT_FALSE(expr->bare_keyword);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* arg = expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(arg->text, "'10:30:00'");
}

TEST(GQLParser, DatetimeLocalTimeNoArgs) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN LOCAL_TIME");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN LOCAL_TIME");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "LOCAL_TIME");
  EXPECT_TRUE(expr->bare_keyword);
  EXPECT_TRUE(expr->children.empty());
}

TEST(GQLParser, DatetimeLocalDatetimeWithString) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN LOCAL_DATETIME('2024-01-15T10:30:00')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN LOCAL_DATETIME('2024-01-15T10:30:00')");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "LOCAL_DATETIME");
  EXPECT_FALSE(expr->bare_keyword);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* arg = expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(arg->text, "'2024-01-15T10:30:00'");
}

TEST(GQLParser, DatetimeLocalTimestampNoArgs) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN LOCAL_TIMESTAMP");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN LOCAL_TIMESTAMP");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "LOCAL_TIMESTAMP");
  EXPECT_TRUE(expr->bare_keyword);
  EXPECT_TRUE(expr->children.empty());
}

TEST(GQLParser, DurationFunctionWithString) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION('P1Y2M')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN DURATION('P1Y2M')");
  ASSERT_GE(clauses->clauses.size(), 2);
  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(expr->text, "DURATION");
  EXPECT_FALSE(expr->bare_keyword);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* arg = expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(arg->text, "'P1Y2M'");
}

// --- Temporal / duration literal syntax (no parentheses) ---

TEST(GQLParser, TemporalLiteralDate) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DATE '2024-01-15'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN DATE '2024-01-15'");

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::TemporalLiteral);
  EXPECT_EQ(expr->text, "DATE");
  ASSERT_EQ(expr->children.size(), 1);
  const auto* val = getExpr(expr->children[0]);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val->text, "'2024-01-15'");
}

TEST(GQLParser, TemporalLiteralTime) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TIME '10:30:00'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN TIME '10:30:00'");

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::TemporalLiteral);
  EXPECT_EQ(expr->text, "TIME");
  ASSERT_EQ(expr->children.size(), 1);
  const auto* val = getExpr(expr->children[0]);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val->text, "'10:30:00'");
}

TEST(GQLParser, TemporalLiteralDatetime) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DATETIME '2024-01-15T10:30:00'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN DATETIME '2024-01-15T10:30:00'");

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::TemporalLiteral);
  EXPECT_EQ(expr->text, "DATETIME");
  ASSERT_EQ(expr->children.size(), 1);
  const auto* val = getExpr(expr->children[0]);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val->text, "'2024-01-15T10:30:00'");
}

TEST(GQLParser, TemporalLiteralTimestamp) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TIMESTAMP '2024-01-15T10:30:00Z'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN TIMESTAMP '2024-01-15T10:30:00Z'");

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::TemporalLiteral);
  EXPECT_EQ(expr->text, "TIMESTAMP");
  ASSERT_EQ(expr->children.size(), 1);
  const auto* val = getExpr(expr->children[0]);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val->text, "'2024-01-15T10:30:00Z'");
}

TEST(GQLParser, DurationLiteral) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION 'P1Y2M'");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN DURATION 'P1Y2M'");

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::DurationLiteral);
  EXPECT_EQ(expr->text, "DURATION");
  ASSERT_EQ(expr->children.size(), 1);
  const auto* val = getExpr(expr->children[0]);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(val->text, "'P1Y2M'");
}

TEST(GQLParser, NumericExpressionInFloor) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN FLOOR(1 + 2 * 3)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* floor_fn = getExpr(ret->items[0]);
  ASSERT_NE(floor_fn, nullptr);
  EXPECT_EQ(floor_fn->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(floor_fn->text, "FLOOR");
  ASSERT_EQ(floor_fn->children.size(), 1);

  const auto* add_op = getExpr(floor_fn->children[0]);
  ASSERT_NE(add_op, nullptr);
  EXPECT_EQ(add_op->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(add_op->text, "+");
  ASSERT_EQ(add_op->children.size(), 2);

  const auto* left = getExpr(add_op->children[0]);
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(left->text, "1");

  const auto* mul_op = getExpr(add_op->children[1]);
  ASSERT_NE(mul_op, nullptr);
  EXPECT_EQ(mul_op->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(mul_op->text, "*");
  ASSERT_EQ(mul_op->children.size(), 2);

  const auto* mul_left = getExpr(mul_op->children[0]);
  ASSERT_NE(mul_left, nullptr);
  EXPECT_EQ(mul_left->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(mul_left->text, "2");

  const auto* mul_right = getExpr(mul_op->children[1]);
  ASSERT_NE(mul_right, nullptr);
  EXPECT_EQ(mul_right->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(mul_right->text, "3");
}

TEST(GQLParser, NumericExpressionInPower) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN POWER(2 + 1, 3 - 1)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* power_fn = getExpr(ret->items[0]);
  ASSERT_NE(power_fn, nullptr);
  EXPECT_EQ(power_fn->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(power_fn->text, "POWER");
  ASSERT_EQ(power_fn->children.size(), 2);

  const auto* base = getExpr(power_fn->children[0]);
  ASSERT_NE(base, nullptr);
  EXPECT_EQ(base->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(base->text, "+");

  const auto* exponent = getExpr(power_fn->children[1]);
  ASSERT_NE(exponent, nullptr);
  EXPECT_EQ(exponent->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(exponent->text, "-");
}

TEST(GQLParser, NumericExpressionUnarySqrt) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN SQRT(-n.x)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* sqrt_fn = getExpr(ret->items[0]);
  ASSERT_NE(sqrt_fn, nullptr);
  EXPECT_EQ(sqrt_fn->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(sqrt_fn->text, "SQRT");
  ASSERT_EQ(sqrt_fn->children.size(), 1);

  const auto* neg = getExpr(sqrt_fn->children[0]);
  ASSERT_NE(neg, nullptr);
  EXPECT_EQ(neg->kind, GAST::GQLExpr::Kind::UnaryOp);
  EXPECT_EQ(neg->text, "-");
  ASSERT_EQ(neg->children.size(), 1);

  const auto* prop = getExpr(neg->children[0]);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ(prop->kind, GAST::GQLExpr::Kind::Property);
  EXPECT_EQ(prop->text, "x");
}

TEST(GQLParser, ElementsFunctionShape) {
  auto ast = parseGraphOrThrow("MATCH p = (a)-[e]->(b) RETURN ELEMENTS(p)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(func->text, "ELEMENTS");
  EXPECT_EQ(func->children.size(), 1);
  EXPECT_EQ(formatAST(*clauses), "MATCH p = (a)-[e]->(b) RETURN ELEMENTS(p)");
}

TEST(GQLParser, DatetimeFunctionCloneRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DATE('2024-01-15')");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, DurationFunctionCloneRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION('P1Y2M')");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, ElementsFunctionRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH p = (a)-[e]->(b) RETURN ELEMENTS(p)");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

// --- wrapper visitor path tests ---

TEST(GQLParser, PathLengthPreservesPathConstructorChild) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN PATH_LENGTH(PATH[n, e, m])");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(func->text, "PATH_LENGTH");
  ASSERT_EQ(func->children.size(), 1);

  const auto* path = getExpr(func->children[0]);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->kind, GAST::GQLExpr::Kind::PathConstructor);
  EXPECT_EQ(path->children.size(), 3);
}

TEST(GQLParser, ElementsPreservesIdentifierChild) {
  auto ast = parseGraphOrThrow("MATCH p = (a)-[e]->(b) RETURN ELEMENTS(p)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(func->text, "ELEMENTS");
  ASSERT_EQ(func->children.size(), 1);

  const auto* arg = getExpr(func->children[0]);
  ASSERT_NE(arg, nullptr);
  EXPECT_EQ(arg->kind, GAST::GQLExpr::Kind::Identifier);
  EXPECT_EQ(arg->text, "p");
}

TEST(GQLParser, DurationBetweenPreservesTemporalLiteralChildren) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION_BETWEEN(DATE '2020-01-01', DATE '2020-01-02')");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  ASSERT_GE(clauses->clauses.size(), 2);

  const auto* ret = clauses->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::DurationBetween);
  ASSERT_EQ(func->children.size(), 2);

  const auto* left = getExpr(func->children[0]);
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->kind, GAST::GQLExpr::Kind::TemporalLiteral);
  EXPECT_EQ(left->text, "DATE");

  const auto* right = getExpr(func->children[1]);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->kind, GAST::GQLExpr::Kind::TemporalLiteral);
  EXPECT_EQ(right->text, "DATE");
}

TEST(GQLParser, ListConstructorPreservesBinaryOpChild) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN [1 + 2, 3]");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* list = getListConstructor(ret->items[0]);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->items.size(), 2);

  const auto* first = getExpr(list->items[0]);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(first->text, "+");

  const auto* second = getExpr(list->items[1]);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->kind, GAST::GQLExpr::Kind::Literal);
  EXPECT_EQ(second->text, "3");
}

TEST(GQLParser, RecordConstructorPreservesBinaryOpValue) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN {a: 1 + 2}");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);

  const auto* record = getRecordConstructor(ret->items[0]);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->fields.size(), 1);

  const auto* field = record->fields[0]->as<GAST::GQLPropertyItem>();
  ASSERT_NE(field, nullptr);
  EXPECT_EQ(field->key, "a");

  const auto* val = getExpr(field->value);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(val->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(val->text, "+");
}

// --- datetimeSubtraction tests ---

TEST(GQLParser, DurationBetweenDayToSecondRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN DURATION_BETWEEN(a.created_at, b.created_at) DAY TO SECOND");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) RETURN DURATION_BETWEEN(a.created_at, b.created_at) DAY TO SECOND");
}

TEST(GQLParser, DurationBetweenYearToMonthRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN DURATION_BETWEEN(a.start, a.end) YEAR TO MONTH");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) RETURN DURATION_BETWEEN(a.start, a.end) YEAR TO MONTH");
}

TEST(GQLParser, DurationBetweenShape) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION_BETWEEN(n.start, n.end) DAY TO SECOND");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::DurationBetween);
  EXPECT_EQ(func->temporal_qualifier, GAST::GQLExpr::TemporalQualifier::DayToSecond);
  EXPECT_EQ(func->children.size(), 2);
}

TEST(GQLParser, DurationBetweenNoQualifierRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION_BETWEEN(n.start, n.end)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::DurationBetween);
  EXPECT_EQ(func->temporal_qualifier, GAST::GQLExpr::TemporalQualifier::None);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN DURATION_BETWEEN(n.start, n.end)");
}

TEST(GQLParser, DurationBetweenCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN DURATION_BETWEEN(n.start, n.end) YEAR TO MONTH");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

// --- trimSingleCharacterOrByteString tests ---

TEST(GQLParser, TrimLeadingFromRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TRIM(LEADING FROM n.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN TRIM(LEADING FROM n.name)");
}

TEST(GQLParser, TrimTrailingCharRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TRIM(TRAILING 'x' FROM n.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN TRIM(TRAILING 'x' FROM n.name)");
}

TEST(GQLParser, TrimBothCharRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TRIM(BOTH ' ' FROM n.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN TRIM(BOTH ' ' FROM n.name)");
}

TEST(GQLParser, TrimSourceOnlyRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TRIM(n.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (n) RETURN TRIM(n.name)");
}

TEST(GQLParser, TrimShape) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TRIM(BOTH 'x' FROM n.name)");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* func = getExpr(ret->items[0]);
  ASSERT_NE(func, nullptr);
  EXPECT_EQ(func->kind, GAST::GQLExpr::Kind::TrimString);
  EXPECT_EQ(func->trim_spec, GAST::GQLExpr::TrimSpec::Both);
  EXPECT_EQ(func->children.size(), 2);
}

TEST(GQLParser, TrimCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH (n) RETURN TRIM(LEADING 'a' FROM n.name)");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

// --- CASE whenOperand structured coverage tests ---

TEST(GQLParser, SimpleCasePropertyOperandRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.status WHEN 'active' THEN 1 WHEN 'inactive' THEN 0 END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  EXPECT_EQ(case_expr->form, GAST::GQLCaseExpr::Form::Simple);
  EXPECT_NE(case_expr->operand, nullptr);

  const auto* operand_expr = case_expr->operand->as<GAST::GQLExpr>();
  ASSERT_NE(operand_expr, nullptr);
  EXPECT_EQ(operand_expr->kind, GAST::GQLExpr::Kind::Property);
}

TEST(GQLParser, SimpleCaseCompOpWhenRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.score WHEN > 90 THEN 'A' WHEN > 80 THEN 'B' ELSE 'C' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 2);

  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(when0->text, ">");
  EXPECT_EQ(when0->children.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN CASE a.score WHEN > 90 THEN 'A' WHEN > 80 THEN 'B' ELSE 'C' END");
}

TEST(GQLParser, SimpleCaseStringLiteralWhenRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.type WHEN 'person' THEN 1 WHEN 'org' THEN 2 ELSE 0 END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN CASE a.type WHEN 'person' THEN 1 WHEN 'org' THEN 2 ELSE 0 END");
}

TEST(GQLParser, SearchedCaseStructuredPredicateRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE WHEN a.age > 18 THEN 'adult' ELSE 'minor' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN CASE WHEN (a.age > 18) THEN 'adult' ELSE 'minor' END");
}

TEST(GQLParser, SimpleCaseClonePreservesWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.status WHEN 'active' THEN 1 ELSE 0 END");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

// --- CASE multi-operand and predicate-part2 tests ---

TEST(GQLParser, SimpleCaseMultiOperandListRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.status WHEN 1, 2, 3 THEN 'ok' ELSE 'bad' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN CASE a.status WHEN 1, 2, 3 THEN 'ok' ELSE 'bad' END");
}

TEST(GQLParser, SimpleCaseMultiOperandListShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.status WHEN 1, 2 THEN 'match' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 1);

  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::ExprList);
  EXPECT_EQ(when0->children.size(), 2);
}

TEST(GQLParser, SimpleCaseNullPredicateWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.name WHEN IS NULL THEN 'unknown' ELSE a.name END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 1);

  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(when0->text, "IS");
  EXPECT_EQ(when0->children.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN CASE a.name WHEN IS NULL THEN 'unknown' ELSE a.name END");
}

TEST(GQLParser, SimpleCaseDirectedPredicateWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN CASE e WHEN IS DIRECTED THEN 'yes' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(when0->text, "IS");
  EXPECT_EQ(when0->children.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) RETURN CASE e WHEN IS DIRECTED THEN 'yes' ELSE 'no' END");
}

TEST(GQLParser, SimpleCaseSourcePredicateWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN CASE a WHEN IS SOURCE OF e THEN 'src' ELSE 'other' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(when0->text, "IS SOURCE OF");
  EXPECT_EQ(when0->children.size(), 2);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) RETURN CASE a WHEN IS SOURCE OF e THEN 'src' ELSE 'other' END");
}

TEST(GQLParser, SimpleCaseMultiOperandCloneIsDeep) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.x WHEN 1, 2, 3 THEN 'a' ELSE 'b' END");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, SimpleCaseMixedOperandTypesRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.score WHEN > 90 THEN 'A' WHEN 50, 60 THEN 'C' ELSE 'F' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 2);

  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::BinaryOp);

  const auto* when1 = case_expr->when_operands[1]->as<GAST::GQLExpr>();
  ASSERT_NE(when1, nullptr);
  EXPECT_EQ(when1->kind, GAST::GQLExpr::Kind::ExprList);
}

TEST(GQLParser, ValueQueryExpressionShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN VALUE { MATCH (b) RETURN b.x }");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::ValueQuery);
  ASSERT_EQ(expr->children.size(), 1);
  const auto* subquery = expr->children[0]->as<GAST::GQLSubquery>();
  EXPECT_NE(subquery, nullptr);
}

TEST(GQLParser, ValueQueryExpressionRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN VALUE { MATCH (b) RETURN b.x }");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN VALUE { MATCH (b) RETURN b.x }");
}

TEST(GQLParser, ValueQueryExpressionClone) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN VALUE { MATCH (b) RETURN b.x }");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, LetValueExpressionShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LET x = a.val IN x + 1 END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::LetExpr);
  ASSERT_GE(expr->children.size(), 2);
  const auto* binding = expr->children[0]->as<GAST::GQLAssignmentItem>();
  EXPECT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "x");
}

TEST(GQLParser, LetValueExpressionRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LET x = a.val IN (x + 1) END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN LET x = a.val IN (x + 1) END");
}

TEST(GQLParser, LetValueExpressionMultiBindingShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LET x = a.v1, y = a.v2 IN (x + y) END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::LetExpr);
  ASSERT_EQ(expr->children.size(), 3);
}

TEST(GQLParser, LetValueExpressionClone) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LET x = a.val IN (x + 1) END");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, PathValueConstructorShape) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN PATH[a, e, b]");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::PathConstructor);
  EXPECT_EQ(expr->children.size(), 3);
}

TEST(GQLParser, PathValueConstructorRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN PATH[a, e, b]");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) RETURN PATH[a, e, b]");
}

TEST(GQLParser, PathValueConstructorClone) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN PATH[a, e, b]");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, NormalizedPredicateShape) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name IS NFC NORMALIZED RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* match = getMatchClause(*clauses);
  ASSERT_NE(match, nullptr);
  ASSERT_NE(match->where, nullptr);
  const auto* where_expr = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where_expr, nullptr);
  const auto* predicate = where_expr->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(predicate->text, "IS");
  ASSERT_EQ(predicate->children.size(), 2);
  const auto* right = predicate->children[1]->as<GAST::GQLExpr>();
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->text, "NFC NORMALIZED");
}

TEST(GQLParser, NormalizedPredicateRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name IS NFC NORMALIZED RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE (a.name IS NFC NORMALIZED) RETURN a");
}

TEST(GQLParser, NormalizedPredicateNotNFKDRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name IS NOT NFKD NORMALIZED RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE (a.name IS NOT NFKD NORMALIZED) RETURN a");
}

TEST(GQLParser, NormalizedPredicateNoFormRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name IS NORMALIZED RETURN a");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) WHERE (a.name IS NORMALIZED) RETURN a");
}

TEST(GQLParser, NormalizedPredicateClone) {
  auto ast = parseGraphOrThrow("MATCH (a) WHERE a.name IS NFC NORMALIZED RETURN a");
  auto cloned = ast->clone();
  EXPECT_EQ(formatAST(*ast), formatAST(*cloned));
}

TEST(GQLParser, SimpleCaseNormalizedPredicateWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.name WHEN IS NFC NORMALIZED THEN 'yes' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 1);

  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(when0->text, "IS");
  EXPECT_EQ(when0->children.size(), 2);
}

TEST(GQLParser, SimpleCaseNormalizedPredicateWhenRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE a.name WHEN IS NFC NORMALIZED THEN 'yes' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a) RETURN CASE a.name WHEN IS NFC NORMALIZED THEN 'yes' ELSE 'no' END");
}

TEST(GQLParser, LetValueExpressionValueKeywordShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN LET VALUE x = a.val IN x END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* expr = getExpr(ret->items[0]);
  ASSERT_NE(expr, nullptr);
  EXPECT_EQ(expr->kind, GAST::GQLExpr::Kind::LetExpr);
  ASSERT_GE(expr->children.size(), 2);
  const auto* binding = expr->children[0]->as<GAST::GQLAssignmentItem>();
  ASSERT_NE(binding, nullptr);
  EXPECT_TRUE(binding->value_keyword);
}

TEST(GQLParser, SearchedCasePathConstructorWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN CASE WHEN PATH[a, e, b] IS NOT NULL THEN 'yes' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  EXPECT_EQ(case_expr->form, GAST::GQLCaseExpr::Form::Searched);
  ASSERT_EQ(case_expr->when_operands.size(), 1);

  const auto* predicate = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(predicate->text, "IS NOT");
  ASSERT_EQ(predicate->children.size(), 2);

  const auto* left = predicate->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->kind, GAST::GQLExpr::Kind::PathConstructor);
  EXPECT_EQ(left->children.size(), 3);

  const auto* right = predicate->children[1]->as<GAST::GQLExpr>();
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->text, "NULL");
}

TEST(GQLParser, SearchedCaseValueQueryWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a) RETURN CASE WHEN VALUE { MATCH (b) RETURN b.x } IS NOT NULL THEN 'yes' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  EXPECT_EQ(case_expr->form, GAST::GQLCaseExpr::Form::Searched);
  ASSERT_EQ(case_expr->when_operands.size(), 1);

  const auto* predicate = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(predicate->text, "IS NOT");
  ASSERT_EQ(predicate->children.size(), 2);

  const auto* left = predicate->children[0]->as<GAST::GQLExpr>();
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->kind, GAST::GQLExpr::Kind::ValueQuery);
  ASSERT_EQ(left->children.size(), 1);
  EXPECT_NE(left->children[0]->as<GAST::GQLSubquery>(), nullptr);

  const auto* right = predicate->children[1]->as<GAST::GQLExpr>();
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(right->text, "NULL");
}

TEST(GQLParser, SimpleCasePathConstructorWhenShape) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN CASE a WHEN PATH[a, e, b] THEN 'path' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);

  const auto* ret = getReturnClause(*clauses);
  ASSERT_NE(ret, nullptr);
  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  ASSERT_EQ(case_expr->when_operands.size(), 1);

  const auto* when0 = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when0, nullptr);
  EXPECT_EQ(when0->kind, GAST::GQLExpr::Kind::PathConstructor);
  EXPECT_EQ(when0->children.size(), 3);
}

TEST(GQLParser, SimpleCasePathConstructorWhenRoundTrip) {
  auto ast = parseGraphOrThrow("MATCH (a)-[e]->(b) RETURN CASE a WHEN PATH[a, e, b] THEN 'path' ELSE 'no' END");
  const auto* clauses = getClausesQuery(ast);
  ASSERT_NE(clauses, nullptr);
  EXPECT_EQ(formatAST(*clauses), "MATCH (a)-[e]->(b) RETURN CASE a WHEN PATH[a, e, b] THEN 'path' ELSE 'no' END");
}

// ---------------------------------------------------------------------------
// ParserGQLQuery (dialect=gql) tests
// ---------------------------------------------------------------------------

namespace {

ASTPtr parseViaDialectParser(const String& query) {
  return parseGQLOrThrow(query);
}

}  // namespace

TEST(GQLParser, DialectParserMatchReturn) {
  auto ast = parseViaDialectParser("MATCH (n) RETURN n");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  EXPECT_GE(sq->clauses.size(), 1);
  EXPECT_EQ(formatAST(*sq), "MATCH (n) RETURN n");
}

TEST(GQLParser, DialectParserMatchWhereReturn) {
  auto ast = parseViaDialectParser("MATCH (n:Person) WHERE n.age > 30 RETURN n.name");
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(formatAST(*ast), "MATCH (n:Person) WHERE n.age > 30 RETURN n.name");
}

TEST(GQLParser, DialectParserEdgePattern) {
  auto ast = parseViaDialectParser("MATCH (a)-[e:KNOWS]->(b) RETURN a, b");
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(formatAST(*ast), "MATCH (a)-[e:KNOWS]->(b) RETURN a, b");
}

TEST(GQLParser, ParseStatementRejectsUnconsumedTokens) {
  try {
    (void)OPENGQL::GQLParserUtils::parseStatement("MATCH (n) RETURN n; MATCH (m) RETURN m");
    FAIL() << "Expected `DB::Exception`";
  } catch (const DB::Exception&) {
  }
}

TEST(GQLParser, DialectParserRejectsClickHouseSet) {
  EXPECT_THROW((void)parseViaDialectParser("SET dialect = 'clickhouse'"), DB::Exception);
  EXPECT_THROW((void)parseViaDialectParser("SET query_language = 'clickhouse'"), DB::Exception);
  EXPECT_THROW((void)parseViaDialectParser("SET max_threads = 1"), DB::Exception);
}

TEST(GQLParser, DialectParserSetAllPropertiesGoesToGQL) {
  auto ast = parseViaDialectParser("SET n = {name: 'Bob', age: 25}");
  ASSERT_NE(ast, nullptr);
  const auto* set_q = ast->as<ASTSetQuery>();
  EXPECT_EQ(set_q, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_EQ(sq->clauses.size(), 1);
  EXPECT_NE(sq->clauses[0]->as<GAST::GQLSetClause>(), nullptr);
}

TEST(GQLParser, DialectParserSetDefaultNonSettingRejects) {
  try {
    (void)parseViaDialectParser("SET n = DEFAULT");
    FAIL() << "Expected `DB::Exception`";
  } catch (const DB::Exception&) {
  }
}

TEST(GQLParser, DialectParserSetMixedRejects) {
  try {
    (void)parseViaDialectParser("SET max_threads = DEFAULT, n.x = 1");
    FAIL() << "Expected `DB::Exception`";
  } catch (const DB::Exception&) {
  }
}

TEST(GQLParser, DialectParserSetLabelGoesToGQL) {
  auto ast = parseViaDialectParser("SET n IS Employee");
  ASSERT_NE(ast, nullptr);
  const auto* set_q = ast->as<ASTSetQuery>();
  EXPECT_EQ(set_q, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_EQ(sq->clauses.size(), 1);
  EXPECT_NE(sq->clauses[0]->as<GAST::GQLSetClause>(), nullptr);
}

TEST(GQLParser, DialectParserSetPropertyGoesToGQL) {
  auto ast = parseViaDialectParser("MATCH (n) SET n.x = 1");
  ASSERT_NE(ast, nullptr);
  const auto* set_q = ast->as<ASTSetQuery>();
  EXPECT_EQ(set_q, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_EQ(sq->clauses.size(), 2);
  EXPECT_NE(sq->clauses[1]->as<GAST::GQLSetClause>(), nullptr);
}

TEST(GQLParser, DialectParserNonDialectSettingGoesToGQL) {
  auto ast = parseViaDialectParser("MATCH (n) SET n.age = 30");
  ASSERT_NE(ast, nullptr);
  const auto* set_q = ast->as<ASTSetQuery>();
  EXPECT_EQ(set_q, nullptr);
  EXPECT_NE(formatAST(*ast).find("SET"), String::npos);
}

TEST(GQLParser, DialectParserBareSetGoesToGQL) {
  auto ast = parseViaDialectParser("SET n.x = 1");
  ASSERT_NE(ast, nullptr);
  const auto* set_q = ast->as<ASTSetQuery>();
  EXPECT_EQ(set_q, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_EQ(sq->clauses.size(), 1);
  EXPECT_NE(sq->clauses[0]->as<GAST::GQLSetClause>(), nullptr);
}

TEST(GQLParser, DialectParserDMLInsert) {
  auto ast = parseViaDialectParser("INSERT (:Person {name: 'Alice', age: 30})");
  ASSERT_NE(ast, nullptr);
  EXPECT_NE(formatAST(*ast).find("INSERT"), String::npos);
}

// -- Dialect parser coverage sweep ------------------------------------------
//
// Tests below verify that ParserGQLQuery can parse common GQL query patterns
// end-to-end with proper AST structure.

TEST(GQLParser, DialectParserCountAggregate) {
  auto ast = parseViaDialectParser("MATCH (n) RETURN count(n)");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 2);
  EXPECT_NE(sq->clauses[0]->as<GAST::GQLMatchClause>(), nullptr);
  const auto* ret = sq->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* count_expr = getExpr(ret->items[0]);
  ASSERT_NE(count_expr, nullptr);
  EXPECT_EQ(count_expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(count_expr->text, "COUNT");
  EXPECT_EQ(count_expr->children.size(), 1);
}

TEST(GQLParser, DialectParserOrderByLimit) {
  auto ast = parseViaDialectParser("MATCH (n) RETURN n ORDER BY n.name LIMIT 10");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 3);
  EXPECT_NE(sq->clauses[0]->as<GAST::GQLMatchClause>(), nullptr);
  EXPECT_NE(sq->clauses[1]->as<GAST::GQLReturnClause>(), nullptr);
  EXPECT_NE(sq->clauses[2]->as<GAST::GQLPageClause>(), nullptr);
  auto fmt = formatAST(*ast);
  EXPECT_NE(fmt.find("ORDER BY"), String::npos);
  EXPECT_NE(fmt.find("LIMIT"), String::npos);
}

TEST(GQLParser, DialectParserUnionAll) {
  auto ast = parseViaDialectParser("MATCH (a) RETURN a UNION ALL MATCH (b) RETURN b");
  ASSERT_NE(ast, nullptr);
  const auto* cq = ast->as<GAST::GQLCombinedQuery>();
  ASSERT_NE(cq, nullptr);
  ASSERT_EQ(cq->queries.size(), 2);
  EXPECT_NE(cq->queries[0]->as<GAST::GQLSingleQuery>(), nullptr);
  EXPECT_NE(cq->queries[1]->as<GAST::GQLSingleQuery>(), nullptr);
  EXPECT_NE(formatAST(*ast).find("UNION ALL"), String::npos);
}

TEST(GQLParser, DialectParserCaseExpression) {
  auto ast = parseViaDialectParser("MATCH (n) RETURN CASE WHEN n.age > 30 THEN 'old' ELSE 'young' END");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 2);
  const auto* ret = sq->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  EXPECT_NE(getCaseExpr(ret->items[0]), nullptr);
  auto fmt = formatAST(*ast);
  EXPECT_NE(fmt.find("CASE"), String::npos);
  EXPECT_NE(fmt.find("WHEN"), String::npos);
  EXPECT_NE(fmt.find("ELSE"), String::npos);
}

TEST(GQLParser, DialectParserSumDistinct) {
  auto ast = parseViaDialectParser("MATCH (n) RETURN sum(DISTINCT n.score)");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 2);
  const auto* ret = sq->clauses[1]->as<GAST::GQLReturnClause>();
  ASSERT_NE(ret, nullptr);
  ASSERT_GE(ret->items.size(), 1);
  const auto* sum_expr = getExpr(ret->items[0]);
  ASSERT_NE(sum_expr, nullptr);
  EXPECT_EQ(sum_expr->kind, GAST::GQLExpr::Kind::FunctionCall);
  EXPECT_EQ(sum_expr->text, "SUM");
  EXPECT_EQ(sum_expr->set_quantifier, GAST::GQLExpr::SetQuantifier::Distinct);
}

TEST(GQLParser, DialectParserAcceptsTrailingSemicolon) {
  const String input = "MATCH (n) RETURN n;";
  const char* pos = input.data();
  const char* end = input.data() + input.size();
  ParserGQLQuery parser;
  auto ast = parseGQLQueryAndMovePosition(parser, pos, end, "", false, 0, 0, 0);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(formatAST(*ast), "MATCH (n) RETURN n");
  EXPECT_EQ(pos, end);
}

TEST(GQLParser, DialectParserRejectsMultiStatementInput) {
  const String input = "MATCH (n) RETURN n; MATCH (m) RETURN m";
  const char* pos = input.data();
  const char* end = input.data() + input.size();
  ParserGQLQuery parser;
  EXPECT_THROW((void)parseGQLQueryAndMovePosition(parser, pos, end, "", true, 0, 0, 0), DB::Exception);
  EXPECT_EQ(pos, input.data());
}

TEST(GQLParser, DialectParserLetClause) {
  auto ast = parseViaDialectParser("LET x = 1 MATCH (n) WHERE n.id = x RETURN n");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 3);
  EXPECT_NE(sq->clauses[0]->as<GAST::GQLLetClause>(), nullptr);
  EXPECT_NE(sq->clauses[1]->as<GAST::GQLMatchClause>(), nullptr);
  auto fmt = formatAST(*ast);
  EXPECT_NE(fmt.find("LET"), String::npos);
}

TEST(GQLParser, DialectParserSelectFromNestedQuery) {
  auto ast = parseViaDialectParser("SELECT a.name FROM { MATCH (a) RETURN a }");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 1);
  const auto* sel = sq->clauses[0]->as<GAST::GQLSelectClause>();
  ASSERT_NE(sel, nullptr);
  ASSERT_GE(sel->items.size(), 1);
  ASSERT_NE(sel->source, nullptr);
  const auto* subq = sel->source->as<GAST::GQLSubquery>();
  ASSERT_NE(subq, nullptr);
  EXPECT_NE(subq->query, nullptr);
  EXPECT_NE(formatAST(*ast).find("SELECT"), String::npos);
  EXPECT_NE(formatAST(*ast).find("FROM"), String::npos);
}

TEST(GQLParser, DialectParserSelectFromGraphNestedQuery) {
  auto ast = parseViaDialectParser("SELECT a.name FROM g { MATCH (a) RETURN a }");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_GE(sq->clauses.size(), 1);
  const auto* sel = sq->clauses[0]->as<GAST::GQLSelectClause>();
  ASSERT_NE(sel, nullptr);
  ASSERT_GE(sel->items.size(), 1);
  ASSERT_NE(sel->source, nullptr);
  const auto* src_item = sel->source->as<GAST::GQLSelectSourceItem>();
  ASSERT_NE(src_item, nullptr);
  EXPECT_NE(src_item->graph_reference, nullptr);
  EXPECT_NE(src_item->source, nullptr);
}

// ---------------------------------------------------------------------------
// Normalized formatAST round-trip idempotency tests
// ---------------------------------------------------------------------------

TEST(GQLParser, RoundTripSimpleMatchReturn) {
  assertNormalizedRoundTrip("MATCH (n) RETURN n");
}

TEST(GQLParser, RoundTripMatchWhereReturn) {
  assertNormalizedRoundTrip("MATCH (n:Person) WHERE n.age > 30 RETURN n.name");
}

TEST(GQLParser, RoundTripEdgePattern) {
  assertNormalizedRoundTrip("MATCH (a)-[e:KNOWS]->(b) RETURN a, b");
}

TEST(GQLParser, RoundTripOrderByLimitOffset) {
  assertNormalizedRoundTrip("MATCH (a) ORDER BY a DESC OFFSET 1 LIMIT 2 RETURN a");
}

TEST(GQLParser, RoundTripOrderByNullOrdering) {
  assertNormalizedRoundTrip("MATCH (a) RETURN a ORDER BY a.name DESC NULLS LAST");
}

TEST(GQLParser, RoundTripArithmeticAndFunctions) {
  assertNormalizedRoundTrip("RETURN 1 + 2 * 3, ABS(-1), FLOOR(3.14)");
}

TEST(GQLParser, RoundTripCastAndSearchedCase) {
  assertNormalizedRoundTrip("MATCH (a) RETURN CAST(a.score AS INT32), CASE WHEN a.x > 0 THEN 1 ELSE 0 END");
}

TEST(GQLParser, RoundTripTemporalDurationLiterals) {
  assertNormalizedRoundTrip("RETURN DATE '2024-01-15', DURATION 'P1Y2M'");
}

TEST(GQLParser, RoundTripAggregates) {
  assertNormalizedRoundTrip("MATCH (n) RETURN COUNT(*), SUM(DISTINCT n.x)");
}

TEST(GQLParser, RoundTripDynamicParamAndSpecialValues) {
  assertNormalizedRoundTrip("MATCH (a) RETURN $x, SESSION_USER, TRUE, NULL");
}

TEST(GQLParser, RoundTripListAndRecordConstructors) {
  assertNormalizedRoundTrip("RETURN [1, 2, 3], RECORD {name: 1}");
}

TEST(GQLParser, RoundTripStringFunctions) {
  assertNormalizedRoundTrip("MATCH (a) RETURN CHAR_LENGTH(a.name), UPPER(a.name)");
}

TEST(GQLParser, ASTContractCanonicalQuery) {
  auto ast = parseViaDialectParser("MATCH (n:Person) WHERE n.age > 30 RETURN n.name");
  assertASTContract(ast);

  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_EQ(query->clauses.size(), 2);

  const auto* match = getMatchClause(*query, 0);
  ASSERT_NE(match, nullptr);
  ASSERT_EQ(match->children.size(), 2);
  ASSERT_NE(match->where, nullptr);

  const auto* where = match->where->as<GAST::GQLWhereClause>();
  ASSERT_NE(where, nullptr);
  const auto* predicate = where->expression->as<GAST::GQLExpr>();
  ASSERT_NE(predicate, nullptr);
  EXPECT_EQ(predicate->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(predicate->text, ">");

  const auto* ret = getReturnClause(*query, 1);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);
  const auto* item = getExpr(ret->items[0]);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLExpr::Kind::Property);
  EXPECT_EQ(item->text, "name");
}

TEST(GQLParser, ASTContractCanonicalPattern) {
  auto ast = parseViaDialectParser("MATCH (a)-[e WHERE e.weight > 0]->(b) KEEP ANY 2 PATHS RETURN e");
  assertASTContract(ast);

  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  const auto* match = getMatchClause(*query, 0);
  ASSERT_NE(match, nullptr);
  ASSERT_EQ(match->children.size(), 3);
  ASSERT_NE(match->keep_clause, nullptr);

  const auto* keep = getKeepClause(match->keep_clause);
  ASSERT_NE(keep, nullptr);
  const auto* prefix = getPathSearchPrefix(keep->path_prefix);
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::Any);
  EXPECT_EQ(prefix->count_kind, GAST::CountKind::Paths);
  ASSERT_NE(prefix->count, nullptr);
  const auto* count = getCountSpec(prefix->count);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->text, "2");

  const auto* path = getOnlyPathPattern(*match);
  ASSERT_NE(path, nullptr);
  const auto* term = getPathTerm(*path);
  ASSERT_NE(term, nullptr);
  ASSERT_EQ(term->factors.size(), 3);
  const auto* edge = term->factors[1]->as<GAST::GQLEdgePattern>();
  ASSERT_NE(edge, nullptr);
  ASSERT_NE(edge->where, nullptr);
}

TEST(GQLParser, ASTContractCanonicalExpression) {
  auto ast = parseViaDialectParser("MATCH (a) RETURN CASE WHEN a.age > 18 THEN 'adult' ELSE 'minor' END");
  assertASTContract(ast);

  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  const auto* ret = getReturnClause(*query, 1);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->items.size(), 1);

  const auto* case_expr = getCaseExpr(ret->items[0]);
  ASSERT_NE(case_expr, nullptr);
  EXPECT_EQ(case_expr->form, GAST::GQLCaseExpr::Form::Searched);
  ASSERT_EQ(case_expr->when_operands.size(), 1);
  ASSERT_EQ(case_expr->then_results.size(), 1);
  ASSERT_NE(case_expr->else_result, nullptr);

  const auto* when = case_expr->when_operands[0]->as<GAST::GQLExpr>();
  ASSERT_NE(when, nullptr);
  EXPECT_EQ(when->kind, GAST::GQLExpr::Kind::BinaryOp);
  EXPECT_EQ(when->text, ">");
}

TEST(GQLParser, ASTContractCanonicalDML) {
  auto ast = parseViaDialectParser("MATCH (n) SET n.x = 1 RETURN n");
  assertASTContract(ast);

  const auto* query = getClausesQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_EQ(query->clauses.size(), 3);

  const auto* set_clause = query->clauses[1]->as<GAST::GQLSetClause>();
  ASSERT_NE(set_clause, nullptr);
  ASSERT_EQ(set_clause->items.size(), 1);
  ASSERT_EQ(set_clause->children.size(), 1);

  const auto* item = set_clause->items[0]->as<GAST::GQLSetItem>();
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->kind, GAST::GQLSetItem::Kind::Property);
  EXPECT_EQ(item->variable, "n");
  EXPECT_EQ(item->property_or_label, "x");
  ASSERT_NE(item->value, nullptr);
}

TEST(GQLParser, ASTContractCanonicalDDL) {
  auto ast = parseViaDialectParser("CREATE GRAPH g ANY AS COPY OF h");
  assertASTContract(ast);

  const auto* query = getSingleQuery(ast);
  ASSERT_NE(query, nullptr);
  ASSERT_EQ(query->clauses.size(), 1);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::Any);
  ASSERT_NE(stmt->name_reference, nullptr);
  ASSERT_NE(stmt->copy_source, nullptr);
  ASSERT_EQ(stmt->children.size(), 2);

  const auto* name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->name, "g");

  const auto* copy_source = stmt->copy_source->as<GAST::GQLGraphExpression>();
  ASSERT_NE(copy_source, nullptr);
  EXPECT_EQ(copy_source->kind, GAST::GQLGraphExpression::Kind::ObjectNameOrBindingVariable);
  EXPECT_EQ(copy_source->text, "h");
}

TEST(GQLParser, CatalogCreateSchema) {
  auto ast = parseDMLOrThrow("CREATE SCHEMA IF NOT EXISTS /foo");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateSchema);
  EXPECT_TRUE(stmt->if_not_exists);
  EXPECT_FALSE(stmt->if_exists);
  EXPECT_FALSE(stmt->or_replace);
  ASSERT_NE(stmt->name_reference, nullptr);
  const auto* schema_ref = stmt->name_reference->as<GAST::GQLSchemaReference>();
  ASSERT_NE(schema_ref, nullptr);
  EXPECT_EQ(schema_ref->kind, GAST::GQLSchemaReference::Kind::AbsolutePath);
  EXPECT_EQ(schema_ref->name, "foo");
  EXPECT_EQ(formatAST(*stmt), "CREATE SCHEMA IF NOT EXISTS /foo");
}

TEST(GQLParser, CatalogDropSchema) {
  auto ast = parseDMLOrThrow("DROP SCHEMA IF EXISTS /foo");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::DropSchema);
  EXPECT_TRUE(stmt->if_exists);
  EXPECT_FALSE(stmt->if_not_exists);
  ASSERT_NE(stmt->name_reference, nullptr);
  const auto* schema_ref = stmt->name_reference->as<GAST::GQLSchemaReference>();
  ASSERT_NE(schema_ref, nullptr);
  EXPECT_EQ(schema_ref->kind, GAST::GQLSchemaReference::Kind::AbsolutePath);
  EXPECT_EQ(schema_ref->name, "foo");
  EXPECT_EQ(formatAST(*stmt), "DROP SCHEMA IF EXISTS /foo");
}

TEST(GQLParser, CatalogCreatePropertyGraphAny) {
  auto ast = parseDMLOrThrow("CREATE PROPERTY GRAPH g ANY");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
  EXPECT_TRUE(stmt->is_property);
  EXPECT_FALSE(stmt->if_not_exists);
  EXPECT_FALSE(stmt->or_replace);
  ASSERT_NE(stmt->name_reference, nullptr);
  const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(obj_name, nullptr);
  EXPECT_EQ(obj_name->name, "g");
  EXPECT_TRUE(obj_name->parent_parts.empty());
  EXPECT_EQ(obj_name->schema_ref, nullptr);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::Any);
  EXPECT_EQ(stmt->source_reference, nullptr);
  EXPECT_EQ(formatAST(*stmt), "CREATE PROPERTY GRAPH g ANY");
}

TEST(GQLParser, CatalogDropPropertyGraph) {
  auto ast = parseDMLOrThrow("DROP PROPERTY GRAPH g");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::DropGraph);
  EXPECT_TRUE(stmt->is_property);
  EXPECT_FALSE(stmt->if_exists);
  ASSERT_NE(stmt->name_reference, nullptr);
  const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(obj_name, nullptr);
  EXPECT_EQ(obj_name->name, "g");
  EXPECT_EQ(formatAST(*stmt), "DROP PROPERTY GRAPH g");
}

TEST(GQLParser, CatalogCreateGraphTypeLikeGraph) {
  auto ast = parseDMLOrThrow("CREATE GRAPH TYPE gt LIKE g");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraphType);
  EXPECT_FALSE(stmt->is_property);
  ASSERT_NE(stmt->name_reference, nullptr);
  const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(obj_name, nullptr);
  EXPECT_EQ(obj_name->name, "gt");
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::LikeGraph);
  ASSERT_NE(stmt->source_reference, nullptr);
  const auto* graph_expr = stmt->source_reference->as<GAST::GQLGraphExpression>();
  ASSERT_NE(graph_expr, nullptr);
  EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH TYPE gt LIKE g");
}

TEST(GQLParser, CatalogCreateGraphTypeCopyOf) {
  auto ast = parseDMLOrThrow("CREATE GRAPH TYPE gt2 COPY OF gt1");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraphType);
  const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(obj_name, nullptr);
  EXPECT_EQ(obj_name->name, "gt2");
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::CopyOfType);
  ASSERT_NE(stmt->source_reference, nullptr);
  const auto* type_ref = stmt->source_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(type_ref, nullptr);
  EXPECT_EQ(type_ref->name, "gt1");
  EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH TYPE gt2 COPY OF gt1");
}

TEST(GQLParser, CatalogCreateGraphTypeNestedSpecBuildsGraphTypeAst) {
  auto ast = parseDMLOrThrow("CREATE GRAPH TYPE gt AS { (:Person {name STRING}) }");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraphType);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::NestedSpec);
  ASSERT_NE(stmt->source_reference, nullptr);

  const auto* graph_type = stmt->source_reference->as<GAST::GQLGraphTypeSpecification>();
  ASSERT_NE(graph_type, nullptr);
  ASSERT_EQ(graph_type->children.size(), 1);

  const auto* node_type = graph_type->children[0]->as<GAST::GQLElementTypeSpecification>();
  ASSERT_NE(node_type, nullptr);
  EXPECT_EQ(node_type->kind, GAST::GQLElementTypeSpecification::Kind::Node);
  ASSERT_EQ(node_type->labels.size(), 1);
  EXPECT_EQ(node_type->labels[0], "Person");
  ASSERT_NE(node_type->properties, nullptr);

  const auto* properties = node_type->properties->as<GAST::GQLTypeExpression>();
  ASSERT_NE(properties, nullptr);
  ASSERT_EQ(properties->children.size(), 1);
  const auto* property = properties->children[0]->as<GAST::GQLTypeExpression>();
  ASSERT_NE(property, nullptr);
  EXPECT_EQ(property->kind, GAST::GQLTypeExpression::Kind::Field);
  EXPECT_EQ(property->name, "name");
  ASSERT_EQ(property->children.size(), 1);
  const auto* property_type = property->children[0]->as<GAST::GQLTypeExpression>();
  ASSERT_NE(property_type, nullptr);
  EXPECT_EQ(property_type->name, "STRING");
  EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH TYPE gt {(:Person {name STRING})}");
  assertASTContract(ast);
}

TEST(GQLParser, CatalogCreateGraphTypedNestedSpecBuildsTypeAst) {
  auto ast = parseDMLOrThrow("CREATE GRAPH g TYPED GRAPH { (:Person)-[:KNOWS {since INT}]->(:Person) }");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::NestedSpec);
  ASSERT_NE(stmt->source_reference, nullptr);

  const auto* graph_ref_type = stmt->source_reference->as<GAST::GQLTypeExpression>();
  ASSERT_NE(graph_ref_type, nullptr);
  EXPECT_EQ(graph_ref_type->kind, GAST::GQLTypeExpression::Kind::GraphReference);
  EXPECT_EQ(graph_ref_type->prefix, GAST::GQLTypeExpression::Prefix::Typed);
  ASSERT_EQ(graph_ref_type->children.size(), 1);

  const auto* graph_type = graph_ref_type->children[0]->as<GAST::GQLGraphTypeSpecification>();
  ASSERT_NE(graph_type, nullptr);
  ASSERT_EQ(graph_type->children.size(), 1);
  const auto* edge_type = graph_type->children[0]->as<GAST::GQLElementTypeSpecification>();
  ASSERT_NE(edge_type, nullptr);
  EXPECT_EQ(edge_type->kind, GAST::GQLElementTypeSpecification::Kind::Edge);
  EXPECT_EQ(edge_type->direction, GAST::EdgeDirection::Right);
  ASSERT_EQ(edge_type->labels.size(), 1);
  EXPECT_EQ(edge_type->labels[0], "KNOWS");
  ASSERT_NE(edge_type->properties, nullptr);
  EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH g TYPED GRAPH {(:Person)-[:KNOWS {since INT}]->(:Person)}");
  assertASTContract(ast);
}

TEST(GQLParser, CatalogCreateGraphLikeGraph) {
  auto ast = parseDMLOrThrow("CREATE GRAPH h LIKE g");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::LikeGraph);
  ASSERT_NE(stmt->source_reference, nullptr);
  const auto* graph_expr = stmt->source_reference->as<GAST::GQLGraphExpression>();
  ASSERT_NE(graph_expr, nullptr);
  EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH h LIKE g");
}

TEST(GQLParser, CatalogCreateGraphOfType) {
  auto ast = parseDMLOrThrow("CREATE GRAPH h gt");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::TypeReference);
  ASSERT_NE(stmt->source_reference, nullptr);
  const auto* type_ref = stmt->source_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(type_ref, nullptr);
  EXPECT_EQ(type_ref->name, "gt");
}

TEST(GQLParser, CatalogQualifiedGraphNameDotParent) {
    auto ast = parseDMLOrThrow("CREATE GRAPH catalog.myGraph ANY");
    ASSERT_NE(ast, nullptr);
    const auto* stmt = getCatalogStatement(ast);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
    ASSERT_NE(stmt->name_reference, nullptr);
    const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
    ASSERT_NE(obj_name, nullptr);
    EXPECT_EQ(obj_name->name, "myGraph");
    EXPECT_EQ(obj_name->schema_ref, nullptr);
    ASSERT_EQ(obj_name->parent_parts.size(), 1);
    EXPECT_EQ(obj_name->parent_parts[0], "catalog");
    EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH catalog.myGraph ANY");
}

TEST(GQLParser, CatalogQualifiedGraphTypeNameDotParent) {
    auto ast = parseDMLOrThrow("DROP GRAPH TYPE catalog.myType");
    ASSERT_NE(ast, nullptr);
    const auto* stmt = getCatalogStatement(ast);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::DropGraphType);
    ASSERT_NE(stmt->name_reference, nullptr);
    const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
    ASSERT_NE(obj_name, nullptr);
    EXPECT_EQ(obj_name->name, "myType");
    EXPECT_EQ(obj_name->schema_ref, nullptr);
    ASSERT_EQ(obj_name->parent_parts.size(), 1);
    EXPECT_EQ(obj_name->parent_parts[0], "catalog");
    EXPECT_EQ(formatAST(*stmt), "DROP GRAPH TYPE catalog.myType");
}

TEST(GQLParser, GraphRefQualifiedInUse) {
    auto ast = parseGraphOrThrow("USE catalog.myGraph RETURN 1");
    ASSERT_NE(ast, nullptr);
    const auto* single = ast->as<GAST::GQLSingleQuery>();
    ASSERT_NE(single, nullptr);
    ASSERT_GE(single->clauses.size(), 2);
    const auto* use = single->clauses[0]->as<GAST::GQLUseClause>();
    ASSERT_NE(use, nullptr);
    ASSERT_NE(use->graph_reference, nullptr);
    const auto* graph_expr = use->graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph_expr, nullptr);
    EXPECT_EQ(graph_expr->kind, GAST::GQLGraphExpression::Kind::QualifiedGraphRef);
    ASSERT_NE(graph_expr->value, nullptr);
    const auto* obj_name = graph_expr->value->as<GAST::GQLCatalogObjectName>();
    ASSERT_NE(obj_name, nullptr);
    EXPECT_EQ(obj_name->name, "myGraph");
    ASSERT_EQ(obj_name->parent_parts.size(), 1);
    EXPECT_EQ(obj_name->parent_parts[0], "catalog");
    EXPECT_EQ(obj_name->schema_ref, nullptr);
    EXPECT_EQ(formatAST(*ast), "USE catalog.myGraph RETURN 1");
}

TEST(GQLParser, GraphRefHomeGraph) {
    auto ast = parseGraphOrThrow("USE HOME_GRAPH RETURN 1");
    ASSERT_NE(ast, nullptr);
    const auto* single = ast->as<GAST::GQLSingleQuery>();
    ASSERT_NE(single, nullptr);
    ASSERT_GE(single->clauses.size(), 2);
    const auto* use = single->clauses[0]->as<GAST::GQLUseClause>();
    ASSERT_NE(use, nullptr);
    ASSERT_NE(use->graph_reference, nullptr);
    const auto* graph_expr = use->graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph_expr, nullptr);
    EXPECT_EQ(graph_expr->kind, GAST::GQLGraphExpression::Kind::HomeGraphRef);
    EXPECT_EQ(graph_expr->value, nullptr);
}

TEST(GQLParser, GraphRefQualifiedInLikeSource) {
    auto ast = parseDMLOrThrow("CREATE GRAPH h LIKE catalog.g");
    ASSERT_NE(ast, nullptr);
    const auto* stmt = getCatalogStatement(ast);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::LikeGraph);
    ASSERT_NE(stmt->source_reference, nullptr);
    const auto* graph_expr = stmt->source_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph_expr, nullptr);
    EXPECT_EQ(graph_expr->kind, GAST::GQLGraphExpression::Kind::QualifiedGraphRef);
    ASSERT_NE(graph_expr->value, nullptr);
    const auto* obj_name = graph_expr->value->as<GAST::GQLCatalogObjectName>();
    ASSERT_NE(obj_name, nullptr);
    EXPECT_EQ(obj_name->name, "g");
    ASSERT_EQ(obj_name->parent_parts.size(), 1);
    EXPECT_EQ(obj_name->parent_parts[0], "catalog");
    EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH h LIKE catalog.g");
}

TEST(GQLParser, CatalogCreateGraphAnyWithCopySource) {
  auto ast = parseDMLOrThrow("CREATE GRAPH g ANY AS COPY OF h");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateGraph);
  EXPECT_EQ(stmt->source_kind, GAST::GQLCatalogStatement::SourceKind::Any);
  EXPECT_EQ(stmt->source_reference, nullptr);
  ASSERT_NE(stmt->copy_source, nullptr);
  const auto* graph_expr = stmt->copy_source->as<GAST::GQLGraphExpression>();
  ASSERT_NE(graph_expr, nullptr);
  EXPECT_EQ(formatAST(*stmt), "CREATE GRAPH g ANY AS COPY OF h");
}

TEST(GQLParser, CatalogDropGraphType) {
  auto ast = parseDMLOrThrow("DROP GRAPH TYPE IF EXISTS gt");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::DropGraphType);
  EXPECT_TRUE(stmt->if_exists);
  EXPECT_FALSE(stmt->is_property);
  ASSERT_NE(stmt->name_reference, nullptr);
  const auto* obj_name = stmt->name_reference->as<GAST::GQLCatalogObjectName>();
  ASSERT_NE(obj_name, nullptr);
  EXPECT_EQ(obj_name->name, "gt");
  EXPECT_EQ(formatAST(*stmt), "DROP GRAPH TYPE IF EXISTS gt");
}

TEST(GQLParser, NestedCallDDLNoLongerThrows) {
  auto ast = parseDMLOrThrow("CALL { DROP GRAPH g }");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
}

TEST(GQLParser, CatalogViaDialectParser) {
  auto ast = parseViaDialectParser("CREATE SCHEMA IF NOT EXISTS /bar");
  ASSERT_NE(ast, nullptr);
  const auto* stmt = getCatalogStatement(ast);
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->kind, GAST::GQLCatalogStatement::Kind::CreateSchema);
  const auto* schema_ref = stmt->name_reference->as<GAST::GQLSchemaReference>();
  ASSERT_NE(schema_ref, nullptr);
  EXPECT_EQ(schema_ref->name, "bar");
}

TEST(GQLParser, CatalogCallProcedureInLinearDDL) {
  auto ast = parseDMLOrThrow("CREATE SCHEMA /foo CALL myproc()");
  ASSERT_NE(ast, nullptr);
  const auto* sq = ast->as<GAST::GQLSingleQuery>();
  ASSERT_NE(sq, nullptr);
  ASSERT_EQ(sq->clauses.size(), 2);

  const auto* ddl = sq->clauses[0]->as<GAST::GQLCatalogStatement>();
  ASSERT_NE(ddl, nullptr);
  EXPECT_EQ(ddl->kind, GAST::GQLCatalogStatement::Kind::CreateSchema);

  const auto* call = sq->clauses[1]->as<GAST::GQLCallNamedClause>();
  ASSERT_NE(call, nullptr);
  ASSERT_NE(call->procedure, nullptr);
}

#endif
