#include <Interpreters/GQL/GQLPlanner.h>

#include <Analyzer/GQL/GQLCombinedQueryNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLFilterNode.h>
#include <Analyzer/GQL/GQLLinearQueryNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Analyzer/GQL/GQLReturnNode.h>
#include <Analyzer/ColumnNode.h>
#include <Analyzer/ConstantNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/IdentifierNode.h>
#include <Analyzer/ListNode.h>
#include <Core/Block.h>
#include <Core/Settings.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/Context.h>
#include <Interpreters/GQL/GQLPlanBuilder.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Parsers/ASTSelectIntersectExceptQuery.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/QueryPlan/DistinctStep.h>
#include <Processors/QueryPlan/ExpressionStep.h>
#include <Processors/QueryPlan/FilterStep.h>
#include <Processors/QueryPlan/Graph/MatchStep.h>
#include <Processors/QueryPlan/IntersectOrExceptStep.h>
#include <Processors/QueryPlan/ReadFromPreparedSource.h>
#include <Processors/Sources/SourceFromSingleChunk.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Processors/QueryPlan/UnionStep.h>
#include <QueryPipeline/Pipe.h>
#include <QueryPipeline/SizeLimits.h>
#include <Common/Exception.h>

#include <optional>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace Setting
{
extern const SettingsOverflowMode distinct_overflow_mode;
extern const SettingsMaxThreads max_threads;
extern const SettingsUInt64 max_bytes_in_distinct;
extern const SettingsUInt64 max_rows_in_distinct;
}

namespace GQL
{
namespace
{

namespace GAST = DB::OPENGQL::AST;

enum class CombinedPlanMode : UInt8
{
    UnionAll,
    UnionDistinct,
    ExceptAll,
    ExceptDistinct,
    IntersectAll,
    IntersectDistinct,
};

Names getHeaderColumnNames(const Block & header)
{
    Names names;
    names.reserve(header.columns());
    for (size_t i = 0; i < header.columns(); ++i)
        names.push_back(header.getByPosition(i).name);

    return names;
}

CombinedPlanMode getCombinedPlanMode(const GAST::GQLCombinedQuery & query)
{
    if (query.queries.size() < 2)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL combined query must contain at least two subqueries");
    if (query.operators.size() + 1 != query.queries.size())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query has inconsistent operator count");

    std::optional<CombinedPlanMode> mode;
    for (const auto operation : query.operators)
    {
        CombinedPlanMode current_mode;
        if (operation == GAST::CombinedQueryOperator::UnionAll)
            current_mode = CombinedPlanMode::UnionAll;
        else if (operation == GAST::CombinedQueryOperator::UnionDistinct)
            current_mode = CombinedPlanMode::UnionDistinct;
        else if (operation == GAST::CombinedQueryOperator::ExceptAll)
            current_mode = CombinedPlanMode::ExceptAll;
        else if (operation == GAST::CombinedQueryOperator::ExceptDistinct)
            current_mode = CombinedPlanMode::ExceptDistinct;
        else if (operation == GAST::CombinedQueryOperator::IntersectAll)
            current_mode = CombinedPlanMode::IntersectAll;
        else if (operation == GAST::CombinedQueryOperator::IntersectDistinct)
            current_mode = CombinedPlanMode::IntersectDistinct;
        else
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL OTHERWISE combined queries are not supported");

        if (mode && *mode != current_mode)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Mixing GQL combined query operators is not supported");

        mode = current_mode;
    }

    if (!mode)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query has no operator");

    return *mode;
}

ASTSelectIntersectExceptQuery::Operator getIntersectOrExceptOperator(CombinedPlanMode mode)
{
    switch (mode)
    {
        case CombinedPlanMode::ExceptAll:
            return ASTSelectIntersectExceptQuery::Operator::EXCEPT_ALL;
        case CombinedPlanMode::ExceptDistinct:
            return ASTSelectIntersectExceptQuery::Operator::EXCEPT_DISTINCT;
        case CombinedPlanMode::IntersectAll:
            return ASTSelectIntersectExceptQuery::Operator::INTERSECT_ALL;
        case CombinedPlanMode::IntersectDistinct:
            return ASTSelectIntersectExceptQuery::Operator::INTERSECT_DISTINCT;
        case CombinedPlanMode::UnionAll:
        case CombinedPlanMode::UnionDistinct:
            break;
    }

    throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query mode is not INTERSECT or EXCEPT");
}

bool needsDistinctStep(CombinedPlanMode mode)
{
    return mode == CombinedPlanMode::UnionDistinct
        || mode == CombinedPlanMode::ExceptDistinct
        || mode == CombinedPlanMode::IntersectDistinct;
}

void buildGQLQueryPlanImpl(
    QueryPlan & query_plan,
    const IAST & query,
    ContextPtr context,
    const PlanScope * initial_scope,
    PlanScope * output_scope);

QueryPlanPtr buildChildPlan(
    const ASTPtr & query,
    ContextPtr context,
    const PlanScope * initial_scope)
{
    if (!query)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined child query is null");

    auto plan = std::make_unique<QueryPlan>();
    buildGQLQueryPlanImpl(*plan, *query, context, initial_scope, nullptr);
    return plan;
}

void addConversionIfNeeded(QueryPlan & plan, const SharedHeader & result_header, ContextPtr context)
{
    if (blocksHaveEqualStructure(*plan.getCurrentHeader(), *result_header))
        return;

    auto actions = ActionsDAG::makeConvertingActions(
        plan.getCurrentHeader()->getColumnsWithTypeAndName(),
        result_header->getColumnsWithTypeAndName(),
        ActionsDAG::MatchColumnsMode::Position,
        context);
    auto converting_step = std::make_unique<ExpressionStep>(plan.getCurrentHeader(), std::move(actions));
    converting_step->setStepDescription("Conversion before GQL UNION");
    plan.addStep(std::move(converting_step));
}

void addDistinctStep(QueryPlan & query_plan, const Names & columns, ContextPtr context)
{
    const auto & settings = context->getSettingsRef();
    SizeLimits limits(
        settings[Setting::max_rows_in_distinct],
        settings[Setting::max_bytes_in_distinct],
        settings[Setting::distinct_overflow_mode]);

    query_plan.addStep(std::make_unique<DistinctStep>(query_plan.getCurrentHeader(), limits, 0, columns, false));
}

void buildSingleQueryPlan(
    QueryPlan & query_plan,
    const GAST::GQLSingleQuery & query,
    ContextPtr context,
    const PlanScope * initial_scope,
    PlanScope * output_scope)
{
    if (initial_scope)
    {
        GQLPlanBuilder builder(context, *initial_scope);
        builder.buildSingleQuery(query_plan, query);
        if (output_scope)
            *output_scope = builder.getScope();
        return;
    }

    GQLPlanBuilder builder(context);
    builder.buildSingleQuery(query_plan, query);
    if (output_scope)
        *output_scope = builder.getScope();
}

void buildCombinedQueryPlan(
    QueryPlan & query_plan,
    const GAST::GQLCombinedQuery & query,
    ContextPtr context,
    const PlanScope * initial_scope,
    PlanScope * output_scope)
{
    const auto mode = getCombinedPlanMode(query);
    const size_t num_plans = query.queries.size();
    std::vector<QueryPlanPtr> plans;
    plans.reserve(num_plans);

    for (const auto & child : query.queries)
        plans.push_back(buildChildPlan(child, context, initial_scope));

    const auto result_header = plans.front()->getCurrentHeader();
    const Names result_columns = getHeaderColumnNames(*result_header);

    SharedHeaders headers;
    headers.reserve(plans.size());
    for (auto & plan : plans)
    {
        addConversionIfNeeded(*plan, result_header, context);
        headers.push_back(plan->getCurrentHeader());
    }

    const auto & settings = context->getSettingsRef();
    if (mode == CombinedPlanMode::UnionAll || mode == CombinedPlanMode::UnionDistinct)
    {
        query_plan.unitePlans(std::make_unique<UnionStep>(std::move(headers), settings[Setting::max_threads]), std::move(plans));
    }
    else
    {
        query_plan.unitePlans(
            std::make_unique<IntersectOrExceptStep>(std::move(headers), getIntersectOrExceptOperator(mode), settings[Setting::max_threads]),
            std::move(plans));
    }

    if (needsDistinctStep(mode))
        addDistinctStep(query_plan, result_columns, context);

    if (output_scope)
        output_scope->replaceWithHeader(*query_plan.getCurrentHeader(), BindingKind::Projection);
}

void buildGQLQueryPlanImpl(
    QueryPlan & query_plan,
    const IAST & query,
    ContextPtr context,
    const PlanScope * initial_scope,
    PlanScope * output_scope)
{
    if (const auto * single_query = query.as<GAST::GQLSingleQuery>())
    {
        buildSingleQueryPlan(query_plan, *single_query, context, initial_scope, output_scope);
        return;
    }

    if (const auto * combined_query = query.as<GAST::GQLCombinedQuery>())
    {
        buildCombinedQueryPlan(query_plan, *combined_query, context, initial_scope, output_scope);
        return;
    }

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL query root: {}", query.getID(' '));
}

void buildGQLQueryPlanFromTree(QueryPlan & plan, const QueryTreeNodePtr & query_tree, ContextPtr context, PlanScope & scope);
void planFilter(QueryPlan & plan, const QueryTreeNodePtr & predicate);

Graph::MatchEdgeDirection convertTreeEdgeDirection(GQLEdgePatternNode::Direction direction)
{
    switch (direction)
    {
        case GQLEdgePatternNode::Direction::Left:
            return Graph::MatchEdgeDirection::Incoming;
        case GQLEdgePatternNode::Direction::Right:
            return Graph::MatchEdgeDirection::Outgoing;
        case GQLEdgePatternNode::Direction::Undirected:
            return Graph::MatchEdgeDirection::Undirected;
        case GQLEdgePatternNode::Direction::LeftOrRight:
            return Graph::MatchEdgeDirection::IncomingOrOutgoing;
        case GQLEdgePatternNode::Direction::LeftOrUndirected:
            return Graph::MatchEdgeDirection::IncomingOrUndirected;
        case GQLEdgePatternNode::Direction::UndirectedOrRight:
            return Graph::MatchEdgeDirection::UndirectedOrOutgoing;
        case GQLEdgePatternNode::Direction::Any:
            return Graph::MatchEdgeDirection::Any;
    }

    throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown GQL edge direction in analyzer planner");
}

Graph::MatchPathSpec buildPathSpecFromTree(const GQLPathPatternNode & path)
{
    Graph::MatchPathSpec spec;
    spec.variable = path.getPathVariable();

    const auto & expression = path.getExpression();
    const auto * term = expression ? expression->as<GQLPathTermNode>() : nullptr;
    if (!term)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner only supports classic path terms");

    const auto & elements = term->getElements().getNodes();
    if (elements.empty() || elements.size() % 2 == 0)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL path term must alternate node and edge factors");

    for (size_t i = 0; i < elements.size(); ++i)
    {
        if (i % 2 == 0)
        {
            const auto * node = elements[i]->as<GQLNodePatternNode>();
            if (!node)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL path factor must be GQLNodePatternNode");
            Graph::MatchNodeSpec node_spec;
            node_spec.variable = node->getElementVariable();
            spec.nodes.push_back(std::move(node_spec));
        }
        else
        {
            const auto * edge = elements[i]->as<GQLEdgePatternNode>();
            if (!edge)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL path factor must be GQLEdgePatternNode");
            Graph::MatchEdgeSpec edge_spec;
            edge_spec.variable = edge->getElementVariable();
            edge_spec.direction = convertTreeEdgeDirection(edge->getDirection());
            spec.edges.push_back(std::move(edge_spec));
        }
    }

    return spec;
}

Graph::MatchSpec buildMatchSpecFromTree(const GQLMatchNode & match_node)
{
    if (match_node.isOptional())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "OPTIONAL MATCH is not supported in GQL analyzer planner");
    if (match_node.getMatchMode() != OPENGQL::AST::GraphMatchMode::None)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH mode is not supported in analyzer planner");

    const auto & patterns = match_node.getPathPatterns().getNodes();
    if (patterns.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL MATCH must contain at least one path pattern");

    Graph::MatchClauseSpec clause;
    clause.paths.reserve(patterns.size());
    for (const auto & pattern : patterns)
    {
        const auto * path = pattern->as<GQLPathPatternNode>();
        if (!path)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL MATCH path pattern must be GQLPathPatternNode");
        clause.paths.push_back(buildPathSpecFromTree(*path));
    }

    Graph::MatchSpec result;
    result.clauses.push_back(std::move(clause));
    return result;
}

void addEmptySingleRowSource(QueryPlan & plan, PlanScope & scope)
{
    auto header = std::make_shared<const Block>();
    Columns columns;
    auto source = std::make_shared<SourceFromSingleChunk>(header, Chunk(std::move(columns), 1));
    plan.addStep(std::make_unique<ReadFromPreparedSource>(Pipe(std::move(source))));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Source);
}

void planMatchFromTree(QueryPlan & plan, const GQLMatchNode & match_node, ContextPtr context, PlanScope & scope)
{
    auto match_spec = buildMatchSpecFromTree(match_node);

    /// A graph storage would be resolved here through DatabaseCatalog from the active graph
    /// scope; until one is registered, the null storage falls back to an empty source inside
    /// MatchStep.
    plan.addStep(std::make_unique<Graph::MatchStep>(std::move(match_spec), nullptr, context));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Source);

    /// MATCH ... WHERE is planned as a post-source FilterStep. The predicate also stays a
    /// candidate for graph-source pushdown later, but until storage consumes it the filter
    /// keeps the semantics correct.
    if (const auto & where = match_node.getWhere())
        planFilter(plan, where);
}

/// Lower a name-resolved GQL expression node into an ActionsDAG node over the current
/// plan header. Identifiers are expected to have been rewritten into ColumnNodes by
/// GQLNameResolutionPass; richer expression kinds (constants, functions) will be added
/// here as the builder starts producing them.
const ActionsDAG::Node & buildActionsNode(const QueryTreeNodePtr & expression, ActionsDAG & dag)
{
    if (const auto * column = expression->as<ColumnNode>())
    {
        const auto & name = column->getColumnName();
        const auto * node = dag.tryFindInOutputs(name);
        if (!node)
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED,
                "GQL expression references binding '{}' not present in the current plan scope",
                name);
        return *node;
    }

    if (const auto * constant = expression->as<ConstantNode>())
    {
        ColumnWithTypeAndName column;
        column.type = constant->getResultType();
        column.column = constant->getColumn();
        column.name = constant->getValueStringRepresentation();
        return dag.addColumn(std::move(column));
    }

    if (const auto * function = expression->as<FunctionNode>())
    {
        const auto & function_arguments = function->getArguments().getNodes();
        ActionsDAG::NodeRawConstPtrs arguments;
        arguments.reserve(function_arguments.size());
        for (const auto & argument : function_arguments)
            arguments.push_back(&buildActionsNode(argument, dag));
        return dag.addFunction(*function, std::move(arguments), {});
    }

    if (expression->as<IdentifierNode>())
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "GQL identifier was not resolved to a column before planning; GQLNameResolutionPass must run first");

    throw Exception(
        ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner does not yet lower this expression node into actions");
}

void planReturnFromTree(QueryPlan & plan, const GQLReturnNode & ret, ContextPtr context, PlanScope & scope)
{
    const auto & items = ret.getItems().getNodes();
    if (items.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL RETURN must project at least one item");

    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag(current_header->getColumnsWithTypeAndName(), false);

    ActionsDAG::NodeRawConstPtrs outputs;
    outputs.reserve(items.size());
    for (const auto & item : items)
    {
        const auto & node = buildActionsNode(item, dag);
        const auto alias = item->hasAlias() ? item->getAlias() : String{};
        if (alias.empty() || alias == node.result_name)
            outputs.push_back(&node);
        else
            outputs.push_back(&dag.addAlias(node, alias));
    }

    dag.getOutputs() = std::move(outputs);
    dag.addMaterializingOutputActions(false);
    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);

    if (ret.isDistinct())
    {
        const auto columns = getHeaderColumnNames(*plan.getCurrentHeader());
        addDistinctStep(plan, columns, context);
    }
}

void planFilter(QueryPlan & plan, const QueryTreeNodePtr & predicate)
{
    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag(current_header->getColumnsWithTypeAndName(), false);

    const auto & filter_node = buildActionsNode(predicate, dag);
    dag.getOutputs().push_back(&filter_node);

    plan.addStep(std::make_unique<FilterStep>(current_header, std::move(dag), filter_node.result_name, true));
}

void planLinearQueryFromTree(QueryPlan & plan, const GQLLinearQueryNode & linear, ContextPtr context, PlanScope & scope)
{
    const auto & steps = linear.getSteps().getNodes();
    bool source_planned = false;
    for (const auto & step : steps)
    {
        if (!step)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL linear query step is null");

        if (const auto * match = step->as<GQLMatchNode>())
        {
            if (source_planned)
                throw Exception(
                    ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner does not yet support multiple source clauses");
            planMatchFromTree(plan, *match, context, scope);
            source_planned = true;
            continue;
        }

        if (const auto * ret = step->as<GQLReturnNode>())
        {
            if (!source_planned)
            {
                addEmptySingleRowSource(plan, scope);
                source_planned = true;
            }
            planReturnFromTree(plan, *ret, context, scope);
            continue;
        }

        if (const auto * filter = step->as<GQLFilterNode>())
        {
            if (!source_planned)
                throw Exception(
                    ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner does not support FILTER before a source clause");
            planFilter(plan, filter->getPredicate());
            continue;
        }

        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner does not yet support this query step");
    }

    if (!source_planned)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner requires a source clause");
}

CombinedPlanMode getCombinedPlanModeFromTree(const GQLCombinedQueryNode & combined)
{
    const auto & queries = combined.getQueries().getNodes();
    const auto & operators = combined.getOperators();
    if (queries.size() < 2)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL combined query must contain at least two subqueries");
    if (operators.size() + 1 != queries.size())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query has inconsistent operator count");

    std::optional<CombinedPlanMode> mode;
    for (const auto op : operators)
    {
        CombinedPlanMode current_mode = CombinedPlanMode::UnionAll;
        switch (op)
        {
            case GQLCombinedQueryNode::CombinedOperator::UNION_ALL:
                current_mode = CombinedPlanMode::UnionAll;
                break;
            case GQLCombinedQueryNode::CombinedOperator::UNION_DISTINCT:
                current_mode = CombinedPlanMode::UnionDistinct;
                break;
            case GQLCombinedQueryNode::CombinedOperator::EXCEPT_ALL:
                current_mode = CombinedPlanMode::ExceptAll;
                break;
            case GQLCombinedQueryNode::CombinedOperator::EXCEPT_DISTINCT:
                current_mode = CombinedPlanMode::ExceptDistinct;
                break;
            case GQLCombinedQueryNode::CombinedOperator::INTERSECT_ALL:
                current_mode = CombinedPlanMode::IntersectAll;
                break;
            case GQLCombinedQueryNode::CombinedOperator::INTERSECT_DISTINCT:
                current_mode = CombinedPlanMode::IntersectDistinct;
                break;
        }

        if (mode && *mode != current_mode)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Mixing GQL combined query operators is not supported");
        mode = current_mode;
    }

    if (!mode)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query has no operator");

    return *mode;
}

void planCombinedQueryFromTree(QueryPlan & plan, const GQLCombinedQueryNode & combined, ContextPtr context)
{
    const auto mode = getCombinedPlanModeFromTree(combined);
    const auto & queries = combined.getQueries().getNodes();

    std::vector<QueryPlanPtr> plans;
    plans.reserve(queries.size());
    for (const auto & child : queries)
    {
        if (!child)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL combined query child is null");

        auto child_plan = std::make_unique<QueryPlan>();
        PlanScope child_scope;
        buildGQLQueryPlanFromTree(*child_plan, child, context, child_scope);
        plans.push_back(std::move(child_plan));
    }

    const auto result_header = plans.front()->getCurrentHeader();
    const Names result_columns = getHeaderColumnNames(*result_header);

    SharedHeaders headers;
    headers.reserve(plans.size());
    for (auto & child_plan : plans)
    {
        addConversionIfNeeded(*child_plan, result_header, context);
        headers.push_back(child_plan->getCurrentHeader());
    }

    const auto & settings = context->getSettingsRef();
    if (mode == CombinedPlanMode::UnionAll || mode == CombinedPlanMode::UnionDistinct)
    {
        plan.unitePlans(std::make_unique<UnionStep>(std::move(headers), settings[Setting::max_threads]), std::move(plans));
    }
    else
    {
        plan.unitePlans(
            std::make_unique<IntersectOrExceptStep>(
                std::move(headers), getIntersectOrExceptOperator(mode), settings[Setting::max_threads]),
            std::move(plans));
    }

    if (needsDistinctStep(mode))
        addDistinctStep(plan, result_columns, context);
}

void buildGQLQueryPlanFromTree(QueryPlan & plan, const QueryTreeNodePtr & query_tree, ContextPtr context, PlanScope & scope)
{
    if (!query_tree)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL QueryTree is null");

    if (const auto * linear = query_tree->as<GQLLinearQueryNode>())
    {
        planLinearQueryFromTree(plan, *linear, context, scope);
        return;
    }

    if (const auto * combined = query_tree->as<GQLCombinedQueryNode>())
    {
        planCombinedQueryFromTree(plan, *combined, context);
        return;
    }

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL analyzer planner does not yet support this query root");
}

}

void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const IAST & query,
    ContextPtr context)
{
    buildGQLQueryPlanImpl(query_plan, query, std::move(context), nullptr, nullptr);
}

void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const IAST & query,
    ContextPtr context,
    PlanScope & scope)
{
    const auto initial_scope = scope;
    buildGQLQueryPlanImpl(query_plan, query, std::move(context), &initial_scope, &scope);
}

// New QueryTree-based interface
void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const QueryTreeNodePtr & query_tree,
    ContextPtr context)
{
    PlanScope scope;
    buildGQLQueryPlanFromTree(query_plan, query_tree, std::move(context), scope);
}

void buildGQLQueryPlan(
    QueryPlan & query_plan,
    const QueryTreeNodePtr & query_tree,
    ContextPtr context,
    PlanScope & scope)
{
    buildGQLQueryPlanFromTree(query_plan, query_tree, std::move(context), scope);
}

}

}
