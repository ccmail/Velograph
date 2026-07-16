#include "config.h"

#if USE_GQL_GRAMMAR

#  include <gtest/gtest.h>

#  include <base/scope_guard.h>

#  include <Common/Exception.h>
#  include <Common/assert_cast.h>
#  include <Common/tests/gtest_global_context.h>
#  include <Common/tests/gtest_global_register.h>
#  include <Columns/ColumnsNumber.h>
#  include <DataTypes/DataTypeArray.h>
#  include <DataTypes/DataTypesNumber.h>
#  include <Interpreters/GQL/AggregationPlanner.h>
#  include <Interpreters/GQL/ClausePlanner.h>
#  include <Interpreters/GQL/ExpressionPlanner.h>
#  include <Interpreters/GQL/GQLPlanBuilder.h>
#  include <Interpreters/GQL/GQLQueryOptions.h>
#  include <Interpreters/InterpreterGQLQuery.h>
#  include <Interpreters/InterpreterGQLQueryAnalyzer.h>
#  include <Interpreters/DatabaseCatalog.h>
#  include <Parsers/ASTFunction.h>
#  include <Parsers/ASTIdentifier.h>
#  include <Parsers/ASTLiteral.h>
#  include <Parsers/graph/GraphAST.h>
#  include <Parsers/graph/ParserGQLQuery.h>
#  include <Processors/QueryPlan/AggregatingStep.h>
#  include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#  include <Processors/QueryPlan/DistinctStep.h>
#  include <Processors/QueryPlan/ExpressionStep.h>
#  include <Processors/QueryPlan/FilterStep.h>
#  include <Processors/QueryPlan/Graph/MatchStep.h>
#  include <Processors/QueryPlan/Graph/MatchVertexStep.h>
#  include <Processors/QueryPlan/LimitStep.h>
#  include <Processors/QueryPlan/Optimizations/QueryPlanOptimizationSettings.h>
#  include <Processors/QueryPlan/Optimizations/expandMatchSteps.h>
#  include <Processors/QueryPlan/QueryPlan.h>
#  include <Processors/QueryPlan/SortingStep.h>
#  include <Processors/Executors/PullingPipelineExecutor.h>
#  include <QueryPipeline/Pipe.h>
#  include <QueryPipeline/QueryPipelineBuilder.h>
#  include <Storages/Graph/GraphStorageEngine.h>
#  include <Storages/Graph/IGraphStorage.h>
#  include <Storages/Graph/StorageEmptyGraph.h>

#  include <algorithm>
#  include <exception>
#  include <utility>
#  include <vector>

using namespace DB;

namespace GAST = DB::OPENGQL::AST;

namespace DB::ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
extern const int UNKNOWN_TABLE;
}

namespace
{

ContextPtr getInterpreterContext()
{
    tryRegisterFunctions();
    return getContext().context;
}

class TestGraphStorage final : public IGraphStorage
{
public:
    explicit TestGraphStorage(const String & database_name)
        : IGraphStorage(StorageID{database_name, "_graph"})
    {
        vertex_header.insert({std::make_shared<DataTypeUInt64>(), GRAPH_COL_ID});
    }

    String getName() const override { return "TestGraph"; }

protected:
    const Block & getGraphHeader(GraphElementKind /*kind*/) const override
    {
        return vertex_header;
    }

    Pipe scanImpl(
        const SharedHeader & /*header*/,
        const IColumn::Filter & /*header_filter*/,
        GraphElementKind /*kind*/,
        size_t /*max_block_size*/,
        size_t /*num_streams*/) override
    {
        return {};
    }

private:
    Block vertex_header;
};

template <typename Callable>
auto withRegisteredGraphStorage(const GraphStoragePtr & storage, Callable && callable)
{
    auto & context = getMutableContext().context;
    const auto database = DatabaseCatalog::instance().getDatabase(context->getCurrentDatabase());
    if (!database)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Current test database is not registered");
    if (database->isTableExist("_graph", context))
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Test graph storage is already registered");

    database->attachTable(context, "_graph", storage, "");
    SCOPE_EXIT({ database->detachTable(context, "_graph"); });

    return std::forward<Callable>(callable)();
}

template <typename Callable>
void expectExceptionCode(Callable && callable, int expected_code)
{
    try
    {
        std::forward<Callable>(callable)();
        FAIL() << "Expected DB::Exception with code " << expected_code;
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), expected_code) << e.displayText();
    }
    catch (const std::exception & e)
    {
        FAIL() << "Expected DB::Exception, got std::exception: " << e.what();
    }
    catch (...)
    {
        FAIL() << "Expected DB::Exception, got an unknown exception";
    }
}

ASTPtr parseGQL(const std::string & query)
{
    ParserGQLQuery parser;
    return parseGQLQuery(parser, query, 0, 0, 0);
}

QueryPlan buildPlan(const std::string & query)
{
    InterpreterGQLQuery interpreter(parseGQL(query), getInterpreterContext());
    QueryPlan plan;
    interpreter.buildQueryPlan(plan);
    return plan;
}

QueryPlan buildPlanWithAnalyzer(const std::string & query, const GQLQueryOptions & options = {})
{
    auto & context = getMutableContext().context;
    auto storage = std::make_shared<TestGraphStorage>(context->getCurrentDatabase());
    return withRegisteredGraphStorage(
        storage,
        [&]
        {
            InterpreterGQLQueryAnalyzer interpreter(parseGQL(query), context, options);
            return std::move(interpreter).extractQueryPlan();
        });
}

Graph::MatchSpec makeSingleVertexMatchSpec(const String & variable)
{
    Graph::MatchNodeSpec node;
    node.variable = variable;

    Graph::MatchPathSpec path;
    path.nodes.push_back(std::move(node));

    Graph::MatchClauseSpec clause;
    clause.paths.push_back(std::move(path));

    Graph::MatchSpec spec;
    spec.clauses.push_back(std::move(clause));
    return spec;
}

QueryPlan makeSingleVertexMatchPlan(const GraphStoragePtr & storage)
{
    QueryPlan plan;
    plan.addStep(std::make_unique<Graph::MatchStep>(
        makeSingleVertexMatchSpec("n"), storage, Names{"n"}, getInterpreterContext()));
    return plan;
}

QueryPlan buildPlanWithGQLPlanBuilder(const std::string & query)
{
    const auto ast = parseGQL(query);
    const auto * single_query = ast->as<GAST::GQLSingleQuery>();
    if (!single_query)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "test query must parse to GQLSingleQuery");

    QueryPlan plan;
    GQL::GQLPlanBuilder(getInterpreterContext()).buildSingleQuery(plan, *single_query);
    return plan;
}

GQL::PlanScope buildScopeWithGQLPlanBuilder(const std::string & query)
{
    const auto ast = parseGQL(query);
    const auto * single_query = ast->as<GAST::GQLSingleQuery>();
    if (!single_query)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "test query must parse to GQLSingleQuery");

    QueryPlan plan;
    GQL::GQLPlanBuilder builder(getInterpreterContext());
    builder.buildSingleQuery(plan, *single_query);
    return builder.getScope();
}

std::vector<GQL::PlanBinding> buildScopeBindingsWithGQLPlanBuilder(const std::string & query)
{
    return buildScopeWithGQLPlanBuilder(query).getBindings();
}

std::vector<String> linearStepNames(const QueryPlan & plan)
{
    std::vector<String> names;
    const auto * node = plan.getRootNode();
    while (node)
    {
        names.push_back(node->step->getName());
        if (node->children.size() != 1)
            break;
        node = node->children.front();
    }
    return names;
}

const Graph::MatchStep * leafMatchStep(const QueryPlan & plan)
{
    const auto * node = plan.getRootNode();
    while (node && node->children.size() == 1)
        node = node->children.front();

    if (!node)
        return nullptr;

    return dynamic_cast<const Graph::MatchStep *>(node->step.get());
}

void collectMatchSteps(const QueryPlan::Node * node, std::vector<const Graph::MatchStep *> & steps)
{
    if (!node)
        return;

    if (const auto * match_step = dynamic_cast<const Graph::MatchStep *>(node->step.get()))
        steps.push_back(match_step);

    for (const auto & child : node->children)
        collectMatchSteps(child, steps);
}

std::vector<const Graph::MatchStep *> collectMatchSteps(const QueryPlan & plan)
{
    std::vector<const Graph::MatchStep *> steps;
    collectMatchSteps(plan.getRootNode(), steps);
    return steps;
}

}

TEST(GQLInterpreter, BareMatchReturnBuildsToScanThenProjection)
{
    const auto plan = buildPlan("MATCH (n) RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);
    ASSERT_EQ(match.paths.front().nodes.size(), 1u);
    EXPECT_EQ(match.paths.front().nodes.front().variable, "n");
    EXPECT_TRUE(match.paths.front().edges.empty());
}

TEST(GQLInterpreter, UnionAllBuildsRootUnionPlan)
{
    const auto plan = buildPlan("RETURN 1 AS v UNION ALL RETURN 2 AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Union"}));
}

TEST(GQLInterpreter, UnionDistinctBuildsUnionThenDistinctPlan)
{
    const auto plan = buildPlan("RETURN 1 AS v UNION RETURN 1 AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Distinct", "Union"}));
}

TEST(GQLInterpreter, ExceptDistinctBuildsIntersectOrExceptPlan)
{
    const auto plan = buildPlan("RETURN 1 AS v EXCEPT RETURN 2 AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Distinct", "IntersectOrExcept"}));
}

TEST(GQLInterpreter, IntersectDistinctBuildsIntersectOrExceptPlan)
{
    const auto plan = buildPlan("RETURN 1 AS v INTERSECT RETURN 1 AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Distinct", "IntersectOrExcept"}));
}

TEST(GQLInterpreter, GQLPlanBuilderPlansSingleQueryWithoutInterpreter)
{
    const auto plan = buildPlanWithGQLPlanBuilder("MATCH (n) WHERE TRUE RETURN n LIMIT 1");

    EXPECT_EQ(
        linearStepNames(plan),
        (std::vector<String>{"Limit", "Expression", "Filter", "GraphMatch"}));
}

TEST(GQLInterpreter, GQLPlanBuilderScopeTracksMatchSourceBindings)
{
    const auto bindings = buildScopeBindingsWithGQLPlanBuilder("MATCH (a)-[r]->(b) RETURN *");

    ASSERT_EQ(bindings.size(), 3u);
    EXPECT_EQ(bindings[0].name, "a");
    EXPECT_EQ(bindings[1].name, "r");
    EXPECT_EQ(bindings[2].name, "b");
    EXPECT_EQ(bindings[0].kind, GQL::BindingKind::Source);
    EXPECT_EQ(bindings[1].kind, GQL::BindingKind::Source);
    EXPECT_EQ(bindings[2].kind, GQL::BindingKind::Source);
}

TEST(GQLInterpreter, GQLPlanBuilderScopeTracksProjectionBindings)
{
    const auto bindings = buildScopeBindingsWithGQLPlanBuilder("MATCH (n) RETURN n AS node_id");

    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].name, "node_id");
    EXPECT_EQ(bindings[0].kind, GQL::BindingKind::Projection);
    ASSERT_NE(bindings[0].type, nullptr);
}

TEST(GQLInterpreter, UseClauseSetsReusablePlanScopeGraph)
{
    const auto scope = buildScopeWithGQLPlanBuilder("USE g RETURN 1 AS x");

    const auto & active_graph = scope.getActiveGraph();
    ASSERT_TRUE(active_graph);

    const auto * graph = active_graph->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "g");

    ASSERT_EQ(scope.getBindings().size(), 1u);
    EXPECT_EQ(scope.getBindings().front().name, "x");
}

TEST(GQLInterpreter, UseClauseFlowsIntoGraphMatchSpec)
{
    const auto plan = buildPlanWithGQLPlanBuilder("USE g MATCH (n) RETURN n");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & graph_reference = match_step->getMatchSpec().graph_reference;
    ASSERT_TRUE(graph_reference);

    const auto * graph = graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "g");

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    EXPECT_NE(cloned_match_step->getMatchSpec().graph_reference.get(), graph_reference.get());
}

TEST(GQLInterpreter, SelectGraphMatchSourceFlowsIntoGraphMatchSpec)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT n FROM g MATCH (n)");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & graph_reference = match_step->getMatchSpec().graph_reference;
    ASSERT_TRUE(graph_reference);

    const auto * graph = graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "g");
}

TEST(GQLInterpreter, SelectGraphMatchSourceListWithSameGraphBuildsToSingleSource)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT a, b FROM g MATCH (a), g MATCH (b) WHERE a = b");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & match_spec = match_step->getMatchSpec();
    ASSERT_EQ(match_spec.clauses.size(), 2u);

    const auto & graph_reference = match_spec.graph_reference;
    ASSERT_TRUE(graph_reference);

    const auto * graph = graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "g");
}

TEST(GQLInterpreter, SelectGraphMatchSourceListWithDifferentGraphsRequiresCompositionModel)
{
    try
    {
        (void)buildPlanWithGQLPlanBuilder("SELECT a, b FROM foo MATCH (a), bar MATCH (b)");
        FAIL() << "Expected multi-graph SELECT FROM source list to require source composition";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("different graph references"), String::npos);
    }
}

TEST(GQLInterpreter, SelectGraphMatchSourceDoesNotLeakUseScopeGraph)
{
    const auto scope = buildScopeWithGQLPlanBuilder("USE outer_graph SELECT n FROM inner_graph MATCH (n)");

    const auto & active_graph = scope.getActiveGraph();
    ASSERT_TRUE(active_graph);

    const auto * graph = active_graph->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "outer_graph");

    ASSERT_EQ(scope.getBindings().size(), 1u);
    EXPECT_EQ(scope.getBindings().front().name, "n");
}

TEST(GQLInterpreter, SelectGraphSubquerySourceFlowsIntoNestedMatchSpec)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT n FROM g { MATCH (n) RETURN n }");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & graph_reference = match_step->getMatchSpec().graph_reference;
    ASSERT_TRUE(graph_reference);

    const auto * graph = graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "g");
}

TEST(GQLInterpreter, UseClauseFlowsIntoInlineCallSubqueryMatchSpec)
{
    const auto plan = buildPlanWithGQLPlanBuilder("USE g CALL { MATCH (n) RETURN n } RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & graph_reference = match_step->getMatchSpec().graph_reference;
    ASSERT_TRUE(graph_reference);

    const auto * graph = graph_reference->as<GAST::GQLGraphExpression>();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->text, "g");
}

TEST(GQLInterpreter, ReturnWithoutMatchUsesReusableProjectionPlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN 1 AS one");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "one");
}

TEST(GQLInterpreter, SelectWithoutMatchUsesReusableProjectionPlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT 1 AS one");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "one");
}

TEST(GQLInterpreter, SelectFromSubqueryUsesReusableSourcePlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT a FROM { RETURN 1 AS a }");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
}

TEST(GQLInterpreter, SelectFromCombinedSubqueryUsesGQLPlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT v FROM { RETURN 1 AS v UNION ALL RETURN 2 AS v }");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Union"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "v");
}

TEST(GQLInterpreter, InlineCallUsesReusableSourcePlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("CALL { RETURN 1 AS a } RETURN a");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
}

TEST(GQLInterpreter, InlineCallEmptyVariableScopeUsesReusableSourcePlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("CALL () { RETURN 1 AS a } RETURN a");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
}

TEST(GQLInterpreter, InlineCallUnavailableVariableScopeThrows)
{
    try
    {
        (void)buildPlanWithGQLPlanBuilder("CALL (a) { RETURN a } RETURN a");
        FAIL() << "Expected missing inline CALL variable import to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("is not available"), String::npos);
    }
}

TEST(GQLInterpreter, InlineCallImportsExpressionBindingFromOuterScope)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT x FROM { VALUE x = 1 CALL (x) { RETURN x } RETURN x }");

    EXPECT_EQ(
        linearStepNames(plan),
        (std::vector<String>{"Expression", "Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
}

TEST(GQLInterpreter, InlineCallAfterMatchUsesReusablePostSourceClausePlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("MATCH (n) CALL (n) { RETURN n AS m } RETURN m");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "GraphMatch"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "m");
}

TEST(GQLInterpreter, InlineCallPostSourceRejectsNestedSource)
{
    try
    {
        (void)buildPlanWithGQLPlanBuilder("MATCH (n) CALL (n) { MATCH (m) RETURN m } RETURN m");
        FAIL() << "Expected post-source inline CALL with nested source to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("source clause"), String::npos);
    }
}

TEST(GQLInterpreter, InlineCallPostSourceRejectsSelectSource)
{
    try
    {
        (void)buildPlanWithGQLPlanBuilder("MATCH (n) CALL (n) { SELECT m FROM g MATCH (m) } RETURN n");
        FAIL() << "Expected post-source inline CALL with SELECT FROM source to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("source clause"), String::npos);
    }
}

TEST(GQLInterpreter, InlineCallValueBindingSeedsNestedReturn)
{
    const auto plan = buildPlanWithGQLPlanBuilder("CALL { VALUE x = 1 RETURN x } RETURN x");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
}

TEST(GQLInterpreter, SubqueryValueBindingSurvivesNestedMatchSourceUntilProjection)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT n, x FROM { VALUE x = 1 MATCH (n) RETURN n, x }");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "GraphMatch"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 2u);
    EXPECT_EQ(header.getByPosition(0).name, "n");
    EXPECT_EQ(header.getByPosition(1).name, "x");
}

TEST(GQLInterpreter, SubqueryValueBindingDoesNotLeakWhenNotReturned)
{
    const auto bindings = buildScopeBindingsWithGQLPlanBuilder("CALL { VALUE x = 1 RETURN 1 AS y } RETURN y");

    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings.front().name, "y");
}

TEST(GQLInterpreter, TypedSubqueryValueBindingUsesReusableTypeCastPlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("CALL { VALUE x INT32 = 1 RETURN x } RETURN x");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
    EXPECT_EQ(header.getByPosition(0).type->getName(), "Int32");
}

TEST(GQLInterpreter, SelectAllFromSubqueryKeepsSourceHeader)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT * FROM { RETURN 1 AS a }");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
}

TEST(GQLInterpreter, SelectFromSubqueryReusesWhereProjectionAndPagePlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT a FROM { RETURN 1 AS a } WHERE a = 1 ORDER BY a LIMIT 1");

    EXPECT_EQ(
        linearStepNames(plan),
        (std::vector<String>{"Limit", "Sorting", "Expression", "Filter", "Expression", "ReadFromPreparedSource"}));
}

TEST(GQLInterpreter, ReturnDistinctUsesReusableDistinctPlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN DISTINCT 1 AS one");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Distinct", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * distinct = dynamic_cast<const DistinctStep *>(root->step.get());
    ASSERT_NE(distinct, nullptr);
    EXPECT_EQ(distinct->getColumnNames(), (Names{"one"}));
}

TEST(GQLInterpreter, SelectDistinctAllFromSubqueryUsesReusableDistinctPlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT DISTINCT * FROM { RETURN 1 AS a }");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Distinct", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * distinct = dynamic_cast<const DistinctStep *>(root->step.get());
    ASSERT_NE(distinct, nullptr);
    EXPECT_EQ(distinct->getColumnNames(), (Names{"a"}));
}

TEST(GQLInterpreter, LetWithoutMatchStartsReusableScalarSource)
{
    const auto plan = buildPlanWithGQLPlanBuilder("LET x = 1 RETURN x");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
}

TEST(GQLInterpreter, LetAssignmentsCanReferenceEarlierBindings)
{
    const auto plan = buildPlanWithGQLPlanBuilder("LET x = 1, y = x + 1 RETURN y");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "y");
}

TEST(GQLInterpreter, TypedLetValueUsesReusableTypeCastPlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("LET VALUE y INT = 1 RETURN y");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "y");
    EXPECT_EQ(header.getByPosition(0).type->getName(), "Int64");
}

TEST(GQLInterpreter, ForWithoutMatchUsesReusableArrayJoinTransform)
{
    const auto plan = buildPlanWithGQLPlanBuilder("FOR x IN [1, 2] RETURN x");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
}

TEST(GQLInterpreter, ForOffsetUsesAlignedArrayJoinTransform)
{
    const auto plan = buildPlanWithGQLPlanBuilder("FOR x IN [1, 2] WITH OFFSET i RETURN x, i");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 2u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
    EXPECT_EQ(header.getByPosition(1).name, "i");
}

TEST(GQLInterpreter, ForOrdinalityUsesAlignedArrayJoinTransform)
{
    const auto plan = buildPlanWithGQLPlanBuilder("FOR x IN [1, 2] WITH ORDINALITY ord RETURN x, ord");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 2u);
    EXPECT_EQ(header.getByPosition(0).name, "x");
    EXPECT_EQ(header.getByPosition(1).name, "ord");
}

TEST(GQLInterpreter, MatchFinishUsesReusableTerminalProjection)
{
    const auto plan = buildPlanWithGQLPlanBuilder("MATCH (n) FINISH");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    EXPECT_EQ(header.columns(), 0u);
}

TEST(GQLInterpreter, UseFinishStartsReusableScalarSource)
{
    const auto plan = buildPlanWithGQLPlanBuilder("USE g FINISH");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    EXPECT_EQ(header.columns(), 0u);
}

TEST(GQLInterpreter, ReturnGroupByUsesReusableAggregationPlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("FOR x IN [1, 1, 2] RETURN x, COUNT(*) AS c GROUP BY x");

    EXPECT_EQ(
        linearStepNames(plan),
        (std::vector<String>{"Expression", "Aggregating", "Expression", "Expression", "ReadFromPreparedSource"}));
}

TEST(GQLInterpreter, SelectHavingUsesReusablePredicatePlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("SELECT SUM(x) AS s FROM { FOR x IN [1, 2] RETURN x } HAVING s > 1");

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_NE(dynamic_cast<const ExpressionStep *>(root->step.get()), nullptr);
    ASSERT_EQ(root->children.size(), 1u);

    const auto * filter = dynamic_cast<const FilterStep *>(root->children.front()->step.get());
    ASSERT_NE(filter, nullptr);
    ASSERT_EQ(root->children.front()->children.size(), 1u);
    EXPECT_NE(dynamic_cast<const AggregatingStep *>(root->children.front()->children.front()->step.get()), nullptr);
}

TEST(GQLInterpreter, NativeClickHouseAggregateFunctionUsesReusableAggregationDetection)
{
    ASTs items;
    items.push_back(make_intrusive<GAST::GQLAliasedItem>(
        makeASTFunction("sum", make_intrusive<ASTIdentifier>("x")),
        "s"));

    EXPECT_TRUE(GQL::hasAggregateProjectionItems(items));
}

TEST(GQLInterpreter, NativeClickHouseGroupByIdentifierUsesReusableAggregationDetection)
{
    auto group_by = make_intrusive<GAST::GQLGroupByClause>();
    group_by->items.push_back(make_intrusive<ASTIdentifier>("x"));
    group_by->children.push_back(group_by->items.back());

    const auto keys = GQL::AggregationPlannerDetail::extractGroupByKeys(group_by.get());

    EXPECT_EQ(keys, Names{"x"});
}

TEST(GQLInterpreter, NativeClickHouseNonCountAggregateRequiresArguments)
{
    auto aggregate = makeASTFunction("sum");

    EXPECT_THROW(
        GQL::AggregationPlannerDetail::getAggregateFunctionInfo(*aggregate),
        DB::Exception);
}

TEST(GQLInterpreter, LetAfterMatchReusesPostSourceTransform)
{
    const auto plan = buildPlanWithGQLPlanBuilder("MATCH (n) LET x = n RETURN x");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Expression", "GraphMatch"}));
}

TEST(GQLInterpreter, ReturnOffsetLimitUsesReusablePagePlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN 1 AS one OFFSET 1 LIMIT 5");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Limit", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * limit = dynamic_cast<const LimitStep *>(root->step.get());
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->getLimit(), 5u);
}

TEST(GQLInterpreter, ReturnOrderByUsesReusablePagePlanning)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN 1 AS a ORDER BY a DESC NULLS LAST LIMIT 5");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Limit", "Sorting", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->children.size(), 1u);
    const auto * sorting = dynamic_cast<const SortingStep *>(root->children.front()->step.get());
    ASSERT_NE(sorting, nullptr);

    const auto & description = sorting->getSortDescription();
    ASSERT_EQ(description.size(), 1u);
    EXPECT_EQ(description[0].column_name, "a");
    EXPECT_EQ(description[0].direction, -1);
    EXPECT_EQ(description[0].nulls_direction, -1);
}

TEST(GQLInterpreter, ReturnOrderByExpressionUsesHiddenSortColumn)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN 1 AS a ORDER BY a + 1 LIMIT 5");

    EXPECT_EQ(
        linearStepNames(plan),
        (std::vector<String>{"Limit", "Expression", "Sorting", "Expression", "Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
}

TEST(GQLInterpreter, ScalarFunctionUsesReusableExpressionPlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN ABS(-1) AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "v");
}

TEST(GQLInterpreter, CastUsesReusableExpressionPlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN CAST(1 AS INT32) AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "v");
    EXPECT_EQ(header.getByPosition(0).type->getName(), "Int32");
}

TEST(GQLInterpreter, CaseUsesGenericExpressionPlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN CASE WHEN TRUE THEN 1 ELSE 0 END AS v");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "v");
}

TEST(GQLInterpreter, ListConstructorUsesGenericExpressionPlanner)
{
    const auto plan = buildPlanWithGQLPlanBuilder("RETURN [1, 2] AS xs");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "xs");
    EXPECT_EQ(header.getByPosition(0).type->getName(), "Array(UInt64)");
}

TEST(GQLInterpreter, NativeClickHousePagingExpressionsUseReusablePlanning)
{
    const auto ast = parseGQL("RETURN 1 AS a");
    const auto * single_query = ast->as<GAST::GQLSingleQuery>();
    ASSERT_NE(single_query, nullptr);

    QueryPlan plan;
    GQL::GQLPlanBuilder builder(getInterpreterContext());
    builder.buildSingleQuery(plan, *single_query);
    auto scope = builder.getScope();

    auto order_by_item = make_intrusive<GAST::GQLOrderByItem>(make_intrusive<ASTIdentifier>("a"), false);
    auto order_by = make_intrusive<GAST::GQLOrderByClause>(ASTs{order_by_item});
    auto page = make_intrusive<GAST::GQLPageClause>();
    page->order_by = order_by;
    page->limit = make_intrusive<ASTLiteral>(Field(UInt64(1)));
    page->children.push_back(page->order_by);
    page->children.push_back(page->limit);

    GQL::planPageClause(plan, *page, getInterpreterContext(), scope);

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Limit", "Sorting", "Expression", "ReadFromPreparedSource"}));
}

TEST(GQLInterpreter, NativeClickHouseLiteralUsesReusableExpressionPlanner)
{
    ActionsDAG dag;
    const auto literal = make_intrusive<ASTLiteral>(Field(UInt64(7)));

    const auto & node = GQL::buildExpressionNode(*literal, dag, getInterpreterContext());

    EXPECT_EQ(node.result_type->getName(), "UInt64");
    ASSERT_TRUE(node.column);
}

TEST(GQLInterpreter, NativeClickHouseIdentifierUsesReusableExpressionPlanner)
{
    NamesAndTypesList inputs;
    inputs.emplace_back("x", std::make_shared<DataTypeUInt64>());
    ActionsDAG dag(inputs);
    const auto identifier = make_intrusive<ASTIdentifier>("x");

    const auto & node = GQL::buildExpressionNode(*identifier, dag, getInterpreterContext());

    EXPECT_EQ(node.result_name, "x");
    EXPECT_EQ(node.result_type->getName(), "UInt64");
}

TEST(GQLInterpreter, NativeClickHouseFunctionUsesReusableExpressionPlanner)
{
    NamesAndTypesList inputs;
    inputs.emplace_back("x", std::make_shared<DataTypeUInt64>());
    ActionsDAG dag(inputs);
    auto function = makeASTFunction(
        "plus",
        make_intrusive<ASTIdentifier>("x"),
        make_intrusive<ASTLiteral>(Field(UInt64(1))));

    const auto & node = GQL::buildExpressionNode(*function, dag, getInterpreterContext());

    EXPECT_EQ(node.result_type->getName(), "UInt64");
}

TEST(GQLInterpreter, MatchWhereReturnLimitChainsAllSteps)
{
    const auto plan = buildPlan("MATCH (n) WHERE n = 1 RETURN n LIMIT 5");

    EXPECT_EQ(
        linearStepNames(plan),
        (std::vector<String>{"Limit", "Expression", "Filter", "GraphMatch"}));
}

TEST(GQLInterpreter, MatchFilterClauseBuildsToStandaloneFilter)
{
    const auto plan = buildPlan("MATCH (n) FILTER n = 1 RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));
}

TEST(GQLInterpreter, ParsedSpecialValuePredicateBuildsToFilter)
{
    const auto plan = buildPlan("MATCH (n) WHERE TRUE RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));
}

TEST(GQLInterpreter, IsNullPredicateBuildsToFilter)
{
    const auto plan = buildPlan("MATCH (n) WHERE n IS NULL RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));
}

TEST(GQLInterpreter, ReturnAliasIsPreservedInProjection)
{
    const auto plan = buildPlan("MATCH (n) RETURN n AS node_id");

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "node_id");
}

TEST(GQLInterpreter, UnsupportedClauseThrowsNotImplemented)
{
    try
    {
        (void)buildPlan("MATCH (n) SET n.x = 1 RETURN n");
        FAIL() << "Expected GQL DML clause to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("GQL SET execution is not supported"), String::npos);
    }
}

TEST(GQLInterpreter, UnsupportedCatalogClauseUsesCatalogBoundary)
{
    try
    {
        (void)buildPlan("CREATE GRAPH g ANY");
        FAIL() << "Expected GQL catalog clause to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("GQL catalog execution is not supported"), String::npos);
    }
}

TEST(GQLInterpreter, UnsupportedNamedCallUsesCallBoundary)
{
    try
    {
        (void)buildPlan("CALL foo() YIELD x RETURN x");
        FAIL() << "Expected GQL named CALL clause to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("GQL named CALL execution is not supported"), String::npos);
    }
}

TEST(GQLInterpreter, UnsupportedPostSourceNamedCallUsesCallBoundary)
{
    try
    {
        (void)buildPlan("MATCH (n) CALL foo() YIELD x RETURN x");
        FAIL() << "Expected post-source GQL named CALL clause to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("GQL named CALL execution is not supported"), String::npos);
    }
}

TEST(GQLInterpreter, UnknownProjectionIdentifierUsesPlanScope)
{
    try
    {
        (void)buildPlan("MATCH (n) RETURN missing");
        FAIL() << "Expected unknown projection identifier to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("current plan scope"), String::npos);
    }
}

TEST(GQLInterpreter, PostSourceClauseBeforeSourceThrowsNotImplemented)
{
    try
    {
        (void)buildPlan("FILTER TRUE RETURN 1");
        FAIL() << "Expected FILTER before a source clause to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("source clause"), String::npos);
    }
}

TEST(GQLInterpreter, OptionalMatchOperandBlockStillRequiresExecutionSemantics)
{
    try
    {
        (void)buildPlan("OPTIONAL { MATCH (a) } RETURN *");
        FAIL() << "Expected OPTIONAL MATCH operand block to be rejected";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("OPTIONAL MATCH"), String::npos);
    }
}

TEST(GQLInterpreter, EdgePatternBuildsToGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (a)-[r]->(b) RETURN a, r, b");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);

    const auto & path = match.paths.front();
    ASSERT_EQ(path.nodes.size(), 2u);
    ASSERT_EQ(path.edges.size(), 1u);
    EXPECT_EQ(path.nodes[0].variable, "a");
    EXPECT_EQ(path.edges[0].variable, "r");
    EXPECT_EQ(path.edges[0].direction, Graph::MatchEdgeDirection::Outgoing);
    EXPECT_EQ(path.nodes[1].variable, "b");
}

TEST(GQLInterpreter, PathAlternationBuildsToGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (a)-[r]->(b) | (c)-[s]->(d) RETURN a, r, b, c, s, d");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);

    const auto & path = match.paths.front();
    EXPECT_EQ(path.alternation_kind, Graph::MatchPathAlternationKind::Union);
    ASSERT_EQ(path.alternatives.size(), 2u);
    ASSERT_EQ(path.alternatives[0].nodes.size(), 2u);
    ASSERT_EQ(path.alternatives[0].edges.size(), 1u);
    ASSERT_EQ(path.alternatives[1].nodes.size(), 2u);
    ASSERT_EQ(path.alternatives[1].edges.size(), 1u);

    const auto & header = *match_step->getOutputHeader();
    ASSERT_EQ(header.columns(), 6u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
    EXPECT_EQ(header.getByPosition(1).name, "r");
    EXPECT_EQ(header.getByPosition(2).name, "b");
    EXPECT_EQ(header.getByPosition(3).name, "c");
    EXPECT_EQ(header.getByPosition(4).name, "s");
    EXPECT_EQ(header.getByPosition(5).name, "d");

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    const auto & cloned_path = cloned_match_step->getMatchSpec().paths.front();
    ASSERT_EQ(cloned_path.alternatives.size(), 2u);
    EXPECT_EQ(cloned_path.alternatives[0].edges.front().variable, "r");
    EXPECT_EQ(cloned_path.alternatives[1].edges.front().variable, "s");
}

TEST(GQLInterpreter, EdgeQuantifierStaysInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (a)-[r]->{2,5}(b) RETURN a, r, b");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);
    ASSERT_EQ(match.paths.front().edges.size(), 1u);

    const auto & edge = match.paths.front().edges.front();
    ASSERT_NE(edge.quantifier, nullptr);
    const auto * quantifier = edge.quantifier->as<GAST::GQLQuantifier>();
    ASSERT_NE(quantifier, nullptr);
    EXPECT_EQ(quantifier->kind, GAST::GQLQuantifier::Kind::Range);
    EXPECT_EQ(quantifier->lower, "2");
    EXPECT_EQ(quantifier->upper, "5");

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    const auto & cloned_edge = cloned_match_step->getMatchSpec().paths.front().edges.front();
    EXPECT_NE(cloned_edge.quantifier.get(), edge.quantifier.get());
}

TEST(GQLInterpreter, PathPrefixStaysInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH ANY SHORTEST (a)-[r]->(b) RETURN a, b");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);

    const auto & path = match.paths.front();
    ASSERT_NE(path.prefix, nullptr);
    const auto * prefix = path.prefix->as<GAST::GQLPathSearchPrefix>();
    ASSERT_NE(prefix, nullptr);
    EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::AnyShortest);

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    const auto & cloned_path = cloned_match_step->getMatchSpec().paths.front();
    EXPECT_NE(cloned_path.prefix.get(), path.prefix.get());
}

TEST(GQLInterpreter, KeepClauseStaysInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (a)-[r]->(b) KEEP ANY 2 PATHS RETURN a");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_TRUE(match.has_keep_clause);
    ASSERT_NE(match.keep_clause, nullptr);

    const auto * keep = match.keep_clause->as<GAST::GQLKeepClause>();
    ASSERT_NE(keep, nullptr);
    ASSERT_NE(keep->path_prefix, nullptr);
    const auto * prefix = keep->path_prefix->as<GAST::GQLPathSearchPrefix>();
    ASSERT_NE(prefix, nullptr);
    EXPECT_EQ(prefix->search_kind, GAST::PathSearchKind::Any);
    EXPECT_EQ(prefix->count_kind, GAST::CountKind::Paths);

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    EXPECT_NE(cloned_match_step->getMatchSpec().keep_clause.get(), match.keep_clause.get());
}

TEST(GQLInterpreter, MatchWhereClauseStaysInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (a)-[r]->(b) WHERE a IS NOT NULL RETURN a");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_NE(match.where_clause, nullptr);

    const auto * where = match.where_clause->as<GAST::GQLWhereClause>();
    ASSERT_NE(where, nullptr);
    EXPECT_EQ(where->type, GAST::GQLWhereClause::Type::Where);
    ASSERT_NE(where->expression, nullptr);

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    EXPECT_NE(cloned_match_step->getMatchSpec().where_clause.get(), match.where_clause.get());
}

TEST(GQLInterpreter, MatchModeStaysInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH DIFFERENT EDGES (a)-[r]->(b) RETURN a");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    EXPECT_TRUE(match.has_match_mode);
    EXPECT_EQ(match.match_mode, Graph::MatchMode::DifferentEdges);

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    EXPECT_EQ(cloned_match_step->getMatchSpec().match_mode, Graph::MatchMode::DifferentEdges);
}

TEST(GQLInterpreter, MatchYieldRestrictsGraphMatchHeader)
{
    const auto plan = buildPlan("MATCH (a)-[r]->(b) YIELD r RETURN r");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_TRUE(match.has_yield_items);
    ASSERT_EQ(match.yield_variables.size(), 1u);
    EXPECT_EQ(match.yield_variables.front(), "r");

    const auto & header = *match_step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "r");

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    ASSERT_EQ(cloned_match_step->getMatchSpec().yield_items.size(), 1u);
    EXPECT_NE(cloned_match_step->getMatchSpec().yield_items.front().get(), match.yield_items.front().get());
}

TEST(GQLInterpreter, ConsecutiveMatchClausesShareGraphMatchStep)
{
    const auto plan = buildPlan("MATCH (a) MATCH (b) RETURN a, b");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.clauses.size(), 2u);
    ASSERT_EQ(match.paths.size(), 2u);

    const auto & header = *match_step->getOutputHeader();
    ASSERT_EQ(header.columns(), 2u);
    EXPECT_EQ(header.getByPosition(0).name, "a");
    EXPECT_EQ(header.getByPosition(1).name, "b");
}

TEST(GQLInterpreter, PathVariableStaysInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH p = (a)-[r]->(b) RETURN a, r, b");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);
    EXPECT_EQ(match.paths.front().variable, "p");
}

TEST(GQLInterpreter, CompoundEdgeDirectionBuildsToGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (a)<-[r]->(b) RETURN a, r, b");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);
    ASSERT_EQ(match.paths.front().edges.size(), 1u);
    EXPECT_EQ(match.paths.front().edges.front().direction, Graph::MatchEdgeDirection::IncomingOrOutgoing);
}

TEST(GQLInterpreter, MatchPatternConstraintsStayInGraphMatchSpec)
{
    const auto plan = buildPlan("MATCH (n:Person {name: 'x'}) RETURN n");

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);
    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.paths.size(), 1u);
    ASSERT_EQ(match.paths.front().nodes.size(), 1u);

    const auto & node = match.paths.front().nodes.front();
    EXPECT_EQ(node.variable, "n");
    EXPECT_NE(node.label_expression, nullptr);
    EXPECT_NE(node.properties, nullptr);

    const auto cloned_step = match_step->clone();
    const auto * cloned_match_step = dynamic_cast<const Graph::MatchStep *>(cloned_step.get());
    ASSERT_NE(cloned_match_step, nullptr);
    const auto & cloned_node = cloned_match_step->getMatchSpec().paths.front().nodes.front();
    EXPECT_NE(cloned_node.label_expression.get(), node.label_expression.get());
    EXPECT_NE(cloned_node.properties.get(), node.properties.get());
}

TEST(GQLQueryTreeAnalyzer, MatchReturnBuildsScanThenProjection)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (n) RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.clauses.size(), 1u);
    ASSERT_EQ(match.clauses.front().paths.size(), 1u);
    ASSERT_EQ(match.clauses.front().paths.front().nodes.size(), 1u);
    EXPECT_EQ(match.clauses.front().paths.front().nodes.front().variable, "n");

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "n");
}

TEST(GQLQueryTreeAnalyzer, EdgeChainBuildsToGraphMatchSpec)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (a)-[r]->(b) RETURN a, r, b");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * match_step = leafMatchStep(plan);
    ASSERT_NE(match_step, nullptr);

    const auto & match = match_step->getMatchSpec();
    ASSERT_EQ(match.clauses.size(), 1u);
    const auto & path = match.clauses.front().paths.front();
    ASSERT_EQ(path.nodes.size(), 2u);
    ASSERT_EQ(path.edges.size(), 1u);
    EXPECT_EQ(path.nodes[0].variable, "a");
    EXPECT_EQ(path.edges[0].variable, "r");
    EXPECT_EQ(path.edges[0].direction, Graph::MatchEdgeDirection::Outgoing);
    EXPECT_EQ(path.nodes[1].variable, "b");
}

TEST(GQLQueryTreeAnalyzer, UnsupportedClauseFailsClosed)
{
    try
    {
        (void)buildPlanWithAnalyzer("MATCH (n) WHERE n = 1 RETURN n");
        FAIL() << "Expected MATCH-level WHERE to be rejected by the analyzer builder";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
    }
}

TEST(GQLQueryTreeAnalyzer, UnionAllOfMatchReturnBuildsUnionPlan)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (n) RETURN n UNION ALL MATCH (m) RETURN m");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Union"}));

    const auto steps = collectMatchSteps(plan);
    EXPECT_EQ(steps.size(), 2u);
}

TEST(GQLQueryTreeAnalyzer, ReturnDistinctAppendsDistinctStep)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (n) RETURN DISTINCT n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Distinct", "Expression", "GraphMatch"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * distinct = dynamic_cast<const DistinctStep *>(root->step.get());
    ASSERT_NE(distinct, nullptr);
    EXPECT_EQ(distinct->getColumnNames(), (Names{"n"}));
}

TEST(GQLQueryTreeAnalyzer, SourceFreeLiteralReturnBuildsScalarSource)
{
    const auto plan = buildPlanWithAnalyzer("RETURN 1 AS one");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto * expression = dynamic_cast<const ExpressionStep *>(root->step.get());
    ASSERT_NE(expression, nullptr);

    const auto & header = *expression->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "one");
}

TEST(GQLQueryTreeAnalyzer, SourceFreeComputedProjectionLowersFunction)
{
    const auto plan = buildPlanWithAnalyzer("RETURN 1 + 2 AS s");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "ReadFromPreparedSource"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "s");
}

TEST(GQLQueryTreeAnalyzer, ComputedProjectionOverMatchSourceLowersFunction)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (n) RETURN n + 1 AS m");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "m");
}

TEST(GQLQueryTreeAnalyzer, MatchWhereBuildsFilterStep)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (n) WHERE n = 1 RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));
}

TEST(GQLQueryTreeAnalyzer, MatchFilterClauseBuildsFilterStep)
{
    const auto plan = buildPlanWithAnalyzer("MATCH (n) FILTER n = 1 RETURN n");

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "Filter", "GraphMatch"}));
}

TEST(GQLQueryTreeAnalyzer, RunOnlyResolveStillBuildsResolvedPlan)
{
    /// ignore_ast_optimizations routes through runOnlyResolve; resolution alone must still
    /// produce a fully resolved, plannable tree.
    const auto plan = buildPlanWithAnalyzer("MATCH (n) RETURN n + 1 AS m", GQLQueryOptions{}.ignoreASTOptimizations());

    EXPECT_EQ(linearStepNames(plan), (std::vector<String>{"Expression", "GraphMatch"}));

    const auto * root = plan.getRootNode();
    ASSERT_NE(root, nullptr);
    const auto & header = *root->step->getOutputHeader();
    ASSERT_EQ(header.columns(), 1u);
    EXPECT_EQ(header.getByPosition(0).name, "m");
}

TEST(GQLQueryTreeAnalyzer, GetSampleBlockReturnsHeaderWithoutExecuting)
{
    auto & context = getMutableContext().context;
    auto storage = std::make_shared<TestGraphStorage>(context->getCurrentDatabase());
    const auto header = withRegisteredGraphStorage(
        storage,
        [&]
        {
            InterpreterGQLQueryAnalyzer interpreter(parseGQL("MATCH (n) RETURN n"), context);
            return interpreter.getSampleBlock();
        });
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(header->columns(), 1u);
    EXPECT_EQ(header->getByPosition(0).name, "n");
}

// --- M1 fail-closed tests ---

/// M1: MATCH (n) RETURN n without a registered graph storage must throw
/// UNKNOWN_TABLE, never silently produce empty results (P3 fail-closed).
TEST(GQLInterpreter, MatchWithoutGraphThrowsUnknownTable)
{
    expectExceptionCode(
        []
        {
            InterpreterGQLQueryAnalyzer interpreter(
                parseGQL("MATCH (n) RETURN n"), getInterpreterContext());
            (void)std::move(interpreter).extractQueryPlan();
        },
        ErrorCodes::UNKNOWN_TABLE);
}

/// OPTIONAL MATCH is preserved in spec but rejected by execution.
TEST(GQLInterpreter, OptionalMatchThrowsNotImplemented)
{
    expectExceptionCode(
        [] { (void)buildPlanWithAnalyzer("OPTIONAL MATCH (n) RETURN n"); },
        ErrorCodes::NOT_IMPLEMENTED);
}

/// M1: a MatchStep reaching expandMatchSteps without resolved storage
/// is a logic error (planner should have resolved storage first).
TEST(GQLInterpreter, ExpandMatchStepsWithoutStorageThrowsLogicalError)
{
    auto step = std::make_unique<Graph::MatchStep>(
        makeSingleVertexMatchSpec("n"), nullptr, Names{"n"}, getInterpreterContext());

    QueryPlan::Nodes nodes;
    auto & plan_node = nodes.emplace_back();
    plan_node.step = std::move(step);

    expectExceptionCode(
        [&] { QueryPlanOptimizations::expandMatchSteps(plan_node, nodes); },
        ErrorCodes::LOGICAL_ERROR);
}

/// M1: edge patterns like (a)-[e]->(b) must throw NOT_IMPLEMENTED during
/// expandMatchSteps, not silently produce empty results.
TEST(GQLInterpreter, ExpandMatchStepsEdgePatternThrowsNotImplemented)
{
    Graph::MatchSpec spec;
    Graph::MatchClauseSpec clause;
    Graph::MatchPathSpec path;
    Graph::MatchNodeSpec node_a;
    node_a.variable = "a";
    Graph::MatchNodeSpec node_b;
    node_b.variable = "b";
    Graph::MatchEdgeSpec edge_spec;
    edge_spec.variable = "e";
    edge_spec.direction = Graph::MatchEdgeDirection::Outgoing;
    path.nodes.push_back(node_a);
    path.nodes.push_back(node_b);
    path.edges.push_back(edge_spec);
    clause.paths.push_back(path);
    spec.clauses.push_back(clause);

    auto storage = std::make_shared<StorageEmptyGraph>();
    auto step = std::make_unique<Graph::MatchStep>(
        std::move(spec), storage, Names{"a", "e", "b"}, getInterpreterContext());

    QueryPlan::Nodes nodes;
    auto & plan_node = nodes.emplace_back();
    plan_node.step = std::move(step);

    expectExceptionCode(
        [&] { QueryPlanOptimizations::expandMatchSteps(plan_node, nodes); },
        ErrorCodes::NOT_IMPLEMENTED);
}

/// Lowering belongs to `QueryPlan::optimize`, but it is independent of the
/// `optimize_plan` setting and remains idempotent when the plan is revisited.
TEST(GQLInterpreter, OptimizeLowersMatchWhenPlanOptimizationsAreDisabled)
{
    auto & context = getMutableContext().context;
    auto storage = std::make_shared<TestGraphStorage>(context->getCurrentDatabase());
    auto plan = makeSingleVertexMatchPlan(storage);

    QueryPlanOptimizationSettings optimization_settings(context);
    optimization_settings.optimize_plan = false;
    plan.optimize(optimization_settings);
    plan.optimize(optimization_settings);

    ASSERT_NE(plan.getRootNode(), nullptr);
    EXPECT_NE(dynamic_cast<const Graph::MatchVertexStep *>(plan.getRootNode()->step.get()), nullptr);
    EXPECT_NE(debugExplainPlan(plan).find("MatchVertex"), String::npos);
}

/// `do_optimize = false` skips `QueryPlan::optimize` entirely, so the pipeline
/// entry point must still lower `MatchStep` before initializing processors.
TEST(GQLInterpreter, BuildPipelineLowersMatchWhenOptimizationIsSkipped)
{
    auto & context = getMutableContext().context;
    auto storage = std::make_shared<TestGraphStorage>(context->getCurrentDatabase());
    auto plan = makeSingleVertexMatchPlan(storage);

    QueryPlanOptimizationSettings optimization_settings(context);
    auto pipeline = plan.buildQueryPipeline(
        optimization_settings,
        BuildQueryPipelineSettings{context},
        /*do_optimize=*/false);

    ASSERT_NE(pipeline, nullptr);
    ASSERT_NE(plan.getRootNode(), nullptr);
    EXPECT_NE(dynamic_cast<const Graph::MatchVertexStep *>(plan.getRootNode()->step.get()), nullptr);
}

/// M1 end-to-end: create a graph, write vertex data, run MATCH (n) RETURN n,
/// and verify the output contains the written vertex ids.
TEST(GQLInterpreter, MatchSingleVertexReturnsRealData)
{
    auto & mutable_context = getMutableContext().context;
    const auto db_name = mutable_context->getCurrentDatabase();
    auto graph_storage = std::make_shared<GraphStorageEngine>(db_name, mutable_context);
    auto ids = withRegisteredGraphStorage(
        graph_storage,
        [&]
        {
            graph_storage->createInternalTables();
            SCOPE_EXIT({ graph_storage->dropInternalTables(); });

            auto id_col = ColumnUInt64::create();
            id_col->insert(10);
            id_col->insert(20);
            id_col->insert(30);

            auto labels_type = std::make_shared<DataTypeArray>(std::make_shared<DataTypeString>());
            auto labels_col = labels_type->createColumn();
            labels_col->insert(Array{"Person"});
            labels_col->insert(Array{"Person"});
            labels_col->insert(Array{"Person"});

            Block input;
            input.insert({std::move(id_col), std::make_shared<DataTypeUInt64>(), "__ID__"});
            input.insert({std::move(labels_col), labels_type, "labels"});
            graph_storage->writeVertex(input);

            InterpreterGQLQueryAnalyzer interpreter(
                parseGQL("MATCH (n) RETURN n"),
                mutable_context,
                GQLQueryOptions{});
            auto block_io = interpreter.execute();

            PullingPipelineExecutor executor(block_io.pipeline);
            Block output;
            std::vector<UInt64> result;
            while (executor.pull(output))
            {
                if (output.rows() == 0)
                    continue;
                const auto & column = assert_cast<const ColumnUInt64 &>(*output.getByName("n").column);
                for (size_t i = 0; i < output.rows(); ++i)
                    result.push_back(column.getElement(i));
            }
            return result;
        });

    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<UInt64>{10, 20, 30}));
}

#endif
