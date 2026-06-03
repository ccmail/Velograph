#include <Interpreters/GQL/ClausePlanner.h>

#include <charconv>
#include <limits>
#include <unordered_set>

#include <Core/Field.h>
#include <Core/SortDescription.h>
#include <Core/Settings.h>
#include <DataTypes/DataTypesNumber.h>
#include <Functions/FunctionFactory.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/Context.h>
#include <Interpreters/GQL/AggregationPlanner.h>
#include <Interpreters/GQL/ExpressionPlanner.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/QueryPlan/DistinctStep.h>
#include <Processors/QueryPlan/ExpressionStep.h>
#include <Processors/QueryPlan/FilterStep.h>
#include <Processors/QueryPlan/LimitStep.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Processors/QueryPlan/SortingStep.h>
#include <QueryPipeline/SizeLimits.h>
#include <Common/Exception.h>

namespace DB
{

namespace Setting
{
extern const SettingsOverflowMode distinct_overflow_mode;
extern const SettingsUInt64 max_bytes_in_distinct;
extern const SettingsUInt64 max_rows_in_distinct;
}

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace GQL
{
namespace
{

namespace GAST = DB::OPENGQL::AST;

ActionsDAG makeCurrentHeaderDAG(const Block & header)
{
    /// GQL plan headers can come from source subqueries whose columns were
    /// originally constants. Downstream clauses must read the row value, not
    /// duplicate the first const value from the header.
    return ActionsDAG(header.getColumnsWithTypeAndName(), false);
}

void planWherePredicate(QueryPlan & plan, const IAST & predicate, ContextPtr context, const PlanScope & scope)
{
    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);

    const auto & filter_node = buildExpressionNode(predicate, dag, context, scope);
    dag.getOutputs().push_back(&filter_node);

    plan.addStep(std::make_unique<FilterStep>(current_header, std::move(dag), filter_node.result_name, true));
}

UInt64 parseUnsignedIntegerText(const String & text, std::string_view context_name)
{
    UInt64 value = 0;
    const char * begin = text.data();
    const char * end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Only unsigned integer literals are supported for GQL {}", context_name);

    return value;
}

UInt64 extractUnsignedIntegerLiteral(const IAST & literal, std::string_view context_name)
{
    if (const auto * gql_literal = literal.as<GAST::GQLExpr>())
    {
        if (gql_literal->kind != GAST::GQLExpr::Kind::Literal || gql_literal->text.empty())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Only unsigned integer literals are supported for GQL {}", context_name);

        return parseUnsignedIntegerText(gql_literal->text, context_name);
    }

    if (const auto * ast_literal = literal.as<ASTLiteral>())
    {
        if (ast_literal->value.getType() == Field::Types::UInt64)
            return ast_literal->value.safeGet<UInt64>();

        if (ast_literal->value.getType() == Field::Types::Int64)
        {
            const auto value = ast_literal->value.safeGet<Int64>();
            if (value >= 0)
                return static_cast<UInt64>(value);
        }
    }

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Only unsigned integer literals are supported for GQL {}", context_name);
}

const ActionsDAG::Node & addUInt64Constant(ActionsDAG & dag, UInt64 value, String name)
{
    ColumnWithTypeAndName column;
    column.type = std::make_shared<DataTypeUInt64>();
    column.name = std::move(name);
    column.column = column.type->createColumnConst(1, value);
    return dag.addColumn(std::move(column));
}

const ActionsDAG::Node & addFunction(
    ActionsDAG & dag,
    ContextPtr context,
    const String & function_name,
    ActionsDAG::NodeRawConstPtrs arguments)
{
    auto function = FunctionFactory::instance().get(function_name, context);
    return dag.addFunction(function, std::move(arguments), {});
}

String makeUniqueInternalName(const Block & header, const String & prefix)
{
    String name = prefix;
    size_t suffix = 0;
    while (header.has(name))
    {
        ++suffix;
        name = fmt::format("{}_{}", prefix, suffix);
    }

    return name;
}

int getNullsDirection(const GAST::GQLOrderByItem & item, int direction)
{
    if (item.null_ordering == GAST::GQLOrderByItem::NullOrdering::NullsFirst)
        return -direction;
    if (item.null_ordering == GAST::GQLOrderByItem::NullOrdering::NullsLast)
        return direction;

    return 1;
}

String getOrderByColumnName(const GAST::GQLOrderByItem & item, const Block & header)
{
    if (const auto * expr = item.expression ? item.expression->as<GAST::GQLExpr>() : nullptr)
    {
        if (expr->kind == GAST::GQLExpr::Kind::Identifier && header.has(expr->text))
            return expr->text;
    }

    if (const auto * identifier = item.expression ? item.expression->as<ASTIdentifier>() : nullptr)
    {
        const auto name = identifier->name();
        if (header.has(name))
            return name;
    }

    return {};
}

String makeHiddenSortColumnName(const Block & header, const std::unordered_set<String> & used_names, size_t index)
{
    String name = fmt::format("__gql_sort_{}", index);
    while (header.has(name) || used_names.find(name) != used_names.end())
    {
        ++index;
        name = fmt::format("__gql_sort_{}", index);
    }

    return name;
}

UInt64 getSortLimit(const GAST::GQLPageClause & page, UInt64 limit, UInt64 offset)
{
    if (!page.limit)
        return 0;
    if (limit > std::numeric_limits<UInt64>::max() - offset)
        return 0;

    return limit + offset;
}

Names getHeaderColumnNames(const Block & header)
{
    Names names;
    names.reserve(header.columns());
    for (size_t i = 0; i < header.columns(); ++i)
        names.push_back(header.getByPosition(i).name);

    return names;
}

void materializeOrderByExpressions(
    QueryPlan & plan,
    const GAST::GQLOrderByClause & order_by,
    ContextPtr context,
    const PlanScope & scope,
    SortDescription & description,
    Names & original_columns,
    bool & has_hidden_columns)
{
    if (order_by.items.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL ORDER BY must contain at least one sort item");

    auto current_header = plan.getCurrentHeader();
    original_columns = getHeaderColumnNames(*current_header);

    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);
    auto outputs = dag.getOutputs();
    std::unordered_set<String> hidden_sort_names;

    description.reserve(order_by.items.size());
    for (const auto & item_ast : order_by.items)
    {
        const auto * item = item_ast ? item_ast->as<GAST::GQLOrderByItem>() : nullptr;
        if (!item)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL ORDER BY item must be GQLOrderByItem");

        const int direction = item->descending ? -1 : 1;
        String column_name = getOrderByColumnName(*item, *current_header);
        if (column_name.empty())
        {
            if (!item->expression)
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL ORDER BY item must wrap an expression");

            const auto & node = buildExpressionNode(*item->expression, dag, context, scope);
            column_name = makeHiddenSortColumnName(*current_header, hidden_sort_names, description.size());
            hidden_sort_names.insert(column_name);
            outputs.push_back(&dag.addAlias(node, column_name));
            has_hidden_columns = true;
        }

        description.emplace_back(std::move(column_name), direction, getNullsDirection(*item, direction));
    }

    if (has_hidden_columns)
    {
        dag.getOutputs() = std::move(outputs);
        plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    }
}

void pruneHiddenSortColumns(QueryPlan & plan, const Names & original_columns, PlanScope & scope)
{
    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);

    ActionsDAG::NodeRawConstPtrs outputs;
    outputs.reserve(original_columns.size());
    for (const auto & column : original_columns)
    {
        const auto * node = dag.tryFindInOutputs(column);
        if (!node)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL ORDER BY hidden-column pruning lost column '{}'", column);

        outputs.push_back(node);
    }

    dag.getOutputs() = std::move(outputs);
    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);
}

void planOrderByClause(
    QueryPlan & plan,
    const GAST::GQLOrderByClause & order_by,
    UInt64 sort_limit,
    ContextPtr context,
    PlanScope & scope)
{
    SortDescription description;
    Names original_columns;
    bool has_hidden_columns = false;
    materializeOrderByExpressions(plan, order_by, context, scope, description, original_columns, has_hidden_columns);

    plan.addStep(std::make_unique<SortingStep>(
        plan.getCurrentHeader(),
        std::move(description),
        sort_limit,
        SortingStep::Settings(context->getSettingsRef())));

    if (has_hidden_columns)
        pruneHiddenSortColumns(plan, original_columns, scope);
}

void planDistinct(QueryPlan & plan, ContextPtr context)
{
    const auto current_header = plan.getCurrentHeader();
    const auto columns = getHeaderColumnNames(*current_header);
    const auto & settings = context->getSettingsRef();
    SizeLimits limits(
        settings[Setting::max_rows_in_distinct],
        settings[Setting::max_bytes_in_distinct],
        settings[Setting::distinct_overflow_mode]);

    plan.addStep(std::make_unique<DistinctStep>(current_header, limits, 0, columns, false));
}

}

void planWhereClause(
    QueryPlan & plan,
    const GAST::GQLWhereClause & where,
    ContextPtr context,
    std::string_view context_name,
    const PlanScope & scope)
{
    switch (where.type)
    {
        case GAST::GQLWhereClause::Type::Where:
        case GAST::GQLWhereClause::Type::Filter:
        case GAST::GQLWhereClause::Type::Having:
            break;
    }

    if (!where.expression)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "{} predicate must be non-null", context_name);

    planWherePredicate(plan, *where.expression, context, scope);
}

void planReturnClause(QueryPlan & plan, const GAST::GQLReturnClause & ret, ContextPtr context, PlanScope & scope)
{
    if (ret.return_all)
    {
        if (ret.group_by)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "RETURN * with GROUP BY is not supported");
        if (ret.distinct)
            planDistinct(plan, context);
        return;
    }

    if (ret.group_by || hasAggregateProjectionItems(ret.items))
    {
        const auto * group_by = ret.group_by ? ret.group_by->as<GAST::GQLGroupByClause>() : nullptr;
        if (ret.group_by && !group_by)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "RETURN GROUP BY node must be GQLGroupByClause");

        planAggregatingProjectionItems(plan, ret.items, group_by, nullptr, context, "RETURN", scope);
    }
    else
    {
        planProjectionItems(plan, ret.items, context, "RETURN", scope);
    }

    if (ret.distinct)
        planDistinct(plan, context);
}

void planSelectClause(
    QueryPlan & plan,
    const GAST::GQLSelectClause & select,
    ContextPtr context,
    PlanScope & scope,
    bool source_was_planned)
{
    if (select.source && !source_was_planned)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT FROM source must be planned before SELECT projection");

    if (select.where)
    {
        if (!source_was_planned)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT WHERE is not supported without a planned source");

        const auto * where = select.where->as<GAST::GQLWhereClause>();
        if (!where)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT WHERE node must be GQLWhereClause");

        planWhereClause(plan, *where, context, "SELECT", scope);
    }

    if (select.select_all)
    {
        if (select.group_by)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT * with GROUP BY is not supported");
        if (select.having)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT * with HAVING is not supported");
        if (!source_was_planned)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT * is not supported without a source");

        if (select.distinct)
            planDistinct(plan, context);

        return;
    }

    if (select.group_by || hasAggregateProjectionItems(select.items))
    {
        const auto * group_by = select.group_by ? select.group_by->as<GAST::GQLGroupByClause>() : nullptr;
        const auto * having = select.having ? select.having->as<GAST::GQLWhereClause>() : nullptr;
        if (select.group_by && !group_by)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT GROUP BY node must be GQLGroupByClause");
        if (select.having && !having)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT HAVING node must be GQLWhereClause");

        planAggregatingProjectionItems(plan, select.items, group_by, having, context, "SELECT", scope);
    }
    else
    {
        if (select.having)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "SELECT HAVING without aggregation is not supported");

        planProjectionItems(plan, select.items, context, "SELECT", scope);
    }

    if (select.distinct)
        planDistinct(plan, context);
}

void planProjectionItems(QueryPlan & plan, const ASTs & items, ContextPtr context, std::string_view context_name, PlanScope & scope)
{
    if (items.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "{} must project at least one item", context_name);

    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);

    ActionsDAG::NodeRawConstPtrs new_outputs;
    new_outputs.reserve(items.size());

    for (const auto & item_ast : items)
    {
        const auto * item = item_ast->as<GAST::GQLAliasedItem>();
        if (!item)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL {} item must be GQLAliasedItem", context_name);

        if (!item->expression)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL {} item must wrap an expression", context_name);

        const auto & node = buildExpressionNode(*item->expression, dag, context, scope);
        if (item->alias.empty())
            new_outputs.push_back(&node);
        else
            new_outputs.push_back(&dag.addAlias(node, item->alias));
    }

    dag.getOutputs() = std::move(new_outputs);
    dag.addMaterializingOutputActions(false);
    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);
}

void planLetClause(QueryPlan & plan, const GAST::GQLLetClause & let, ContextPtr context, PlanScope & scope)
{
    if (let.items.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "LET must contain at least one assignment");

    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);
    auto & outputs = dag.getOutputs();
    PlanScope local_scope = scope;
    std::unordered_set<String> assigned_names;

    for (const auto & item_ast : let.items)
    {
        const auto * item = item_ast ? item_ast->as<GAST::GQLAssignmentItem>() : nullptr;
        if (!item)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL LET item must be GQLAssignmentItem");
        if (item->name.empty())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL LET assignment name must be non-empty");
        if (!item->value)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL LET assignment '{}' must have a value", item->name);
        if (scope.hasBinding(item->name))
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL LET assignment '{}' conflicts with an existing binding", item->name);
        if (!assigned_names.insert(item->name).second)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Duplicate GQL LET assignment '{}'", item->name);

        const ActionsDAG::Node * value = nullptr;
        if (item->type)
        {
            const auto * type = item->type->as<GAST::GQLTypeExpression>();
            if (!type)
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL LET assignment type must be GQLTypeExpression");

            value = &buildExpressionNodeAsType(*item->value, *type, dag, context, local_scope);
        }
        else
        {
            value = &buildExpressionNode(*item->value, dag, context, local_scope);
        }

        const auto & alias = dag.addAlias(*value, item->name);
        outputs.push_back(&alias);
        local_scope.addOrReplaceBinding(item->name, alias.result_type, BindingKind::Projection);
    }

    dag.addMaterializingOutputActions(false);
    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);
}

void planForClause(QueryPlan & plan, const GAST::GQLForClause & for_clause, ContextPtr context, PlanScope & scope)
{
    if (for_clause.alias.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL FOR alias must be non-empty");
    if (!for_clause.source)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL FOR source must be non-null");
    if (scope.hasBinding(for_clause.alias))
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL FOR alias '{}' conflicts with an existing binding", for_clause.alias);
    if ((for_clause.with_ordinality || for_clause.with_offset) && for_clause.ordinality_or_offset_alias.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL FOR WITH OFFSET/ORDINALITY alias must be non-empty");
    if (!for_clause.ordinality_or_offset_alias.empty())
    {
        if (!for_clause.with_ordinality && !for_clause.with_offset)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL FOR ordinality/offset alias requires WITH OFFSET or WITH ORDINALITY");
        if (for_clause.ordinality_or_offset_alias == for_clause.alias)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL FOR ordinality/offset alias must differ from element alias");
        if (scope.hasBinding(for_clause.ordinality_or_offset_alias))
            throw Exception(
                ErrorCodes::NOT_IMPLEMENTED,
                "GQL FOR ordinality/offset alias '{}' conflicts with an existing binding",
                for_clause.ordinality_or_offset_alias);
    }

    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);
    auto & outputs = dag.getOutputs();

    const auto & source = buildExpressionNode(*for_clause.source, dag, context, scope);

    if (for_clause.with_ordinality || for_clause.with_offset)
    {
        const auto & ordinality_array = addFunction(dag, context, "arrayEnumerate", {&source});
        const auto & zipped = addFunction(dag, context, "arrayZip", {&source, &ordinality_array});
        const auto & pair = dag.addArrayJoin(zipped, makeUniqueInternalName(*current_header, "__gql_for_pair"));
        const auto & first_index = addUInt64Constant(dag, 1, "__gql_for_first_index");
        const auto & second_index = addUInt64Constant(dag, 2, "__gql_for_second_index");
        const auto & element = addFunction(dag, context, "tupleElement", {&pair, &first_index});
        const auto & ordinality = addFunction(dag, context, "tupleElement", {&pair, &second_index});
        const auto & element_alias = dag.addAlias(element, for_clause.alias);
        outputs.push_back(&element_alias);

        const ActionsDAG::Node * position = &ordinality;
        if (for_clause.with_offset)
        {
            const auto & one = addUInt64Constant(dag, 1, "__gql_for_offset_base");
            position = &addFunction(dag, context, "minus", {&ordinality, &one});
        }

        const auto & position_alias = dag.addAlias(*position, for_clause.ordinality_or_offset_alias);
        outputs.push_back(&position_alias);
    }
    else
    {
        const auto & alias = dag.addArrayJoin(source, for_clause.alias);
        outputs.push_back(&alias);
    }

    dag.addMaterializingOutputActions(false);
    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);
}

void planPageClause(QueryPlan & plan, const GAST::GQLPageClause & page, ContextPtr context, PlanScope & scope)
{
    UInt64 offset = 0;
    if (page.offset)
        offset = extractUnsignedIntegerLiteral(*page.offset, "OFFSET");

    UInt64 limit = std::numeric_limits<UInt64>::max();
    if (page.limit)
        limit = extractUnsignedIntegerLiteral(*page.limit, "LIMIT");

    if (!page.order_by && !page.offset && !page.limit)
    {
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL page clause without OFFSET or LIMIT is not supported");
    }

    if (page.order_by)
    {
        const auto * order_by = page.order_by->as<GAST::GQLOrderByClause>();
        if (!order_by)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL ORDER BY node must be GQLOrderByClause");

        planOrderByClause(plan, *order_by, getSortLimit(page, limit, offset), context, scope);
    }

    if (page.offset || page.limit)
        plan.addStep(std::make_unique<LimitStep>(plan.getCurrentHeader(), static_cast<size_t>(limit), static_cast<size_t>(offset)));
}

void planFinishClause(QueryPlan & plan, const GAST::GQLFinishClause &, PlanScope & scope)
{
    auto current_header = plan.getCurrentHeader();
    ActionsDAG dag = makeCurrentHeaderDAG(*current_header);
    dag.getOutputs().clear();
    dag.addMaterializingOutputActions(false);

    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(dag)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);
}

void planClauseTransform(QueryPlan & plan, const ASTPtr & clause_ast, ContextPtr context, PlanScope & scope)
{
    if (!clause_ast)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL clause node is null");

    if (const auto * ret = clause_ast->as<GAST::GQLReturnClause>())
    {
        planReturnClause(plan, *ret, context, scope);
        return;
    }

    if (const auto * page = clause_ast->as<GAST::GQLPageClause>())
    {
        planPageClause(plan, *page, context, scope);
        return;
    }

    if (const auto * where = clause_ast->as<GAST::GQLWhereClause>())
    {
        planWhereClause(plan, *where, context, "GQL FILTER", scope);
        return;
    }

    if (const auto * let = clause_ast->as<GAST::GQLLetClause>())
    {
        planLetClause(plan, *let, context, scope);
        return;
    }

    if (const auto * for_clause = clause_ast->as<GAST::GQLForClause>())
    {
        planForClause(plan, *for_clause, context, scope);
        return;
    }

    if (const auto * finish = clause_ast->as<GAST::GQLFinishClause>())
    {
        planFinishClause(plan, *finish, scope);
        return;
    }

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL clause: {}", clause_ast->getID(' '));
}

}

}
