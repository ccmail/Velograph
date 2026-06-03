#pragma once

#include <AggregateFunctions/AggregateFunctionFactory.h>
#include <Core/Settings.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/AggregateDescription.h>
#include <Interpreters/Aggregator.h>
#include <Interpreters/Context.h>
#include <Interpreters/GQL/ExpressionPlanner.h>
#include <Interpreters/GQL/PlanScope.h>
#include <Interpreters/HashTablesStatistics.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/NullsAction.h>
#include <Parsers/graph/GraphAST.h>
#include <Processors/QueryPlan/AggregatingStep.h>
#include <Processors/QueryPlan/ExpressionStep.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Common/Exception.h>

#include <Poco/String.h>

#include <optional>
#include <string_view>

namespace DB
{

namespace Setting
{
extern const SettingsUInt64 aggregation_in_order_max_block_bytes;
extern const SettingsBool compile_aggregate_expressions;
extern const SettingsBool empty_result_for_aggregation_by_constant_keys_on_empty_set;
extern const SettingsBool empty_result_for_aggregation_by_empty_set;
extern const SettingsBool enable_producing_buckets_out_of_order_in_aggregation;
extern const SettingsBool enable_software_prefetch_in_aggregation;
extern const SettingsOverflowModeGroupBy group_by_overflow_mode;
extern const SettingsBool group_by_use_nulls;
extern const SettingsUInt64 group_by_two_level_threshold;
extern const SettingsUInt64 group_by_two_level_threshold_bytes;
extern const SettingsUInt64 max_bytes_before_external_group_by;
extern const SettingsDouble max_bytes_ratio_before_external_group_by;
extern const SettingsNonZeroUInt64 max_block_size;
extern const SettingsUInt64 max_rows_to_group_by;
extern const SettingsMaxThreads max_threads;
extern const SettingsUInt64 min_count_to_compile_aggregate_expression;
extern const SettingsUInt64 min_free_disk_space_for_temporary_data;
extern const SettingsFloat min_hit_rate_to_use_consecutive_keys_optimization;
extern const SettingsBool optimize_group_by_constant_keys;
extern const SettingsBool serialize_string_in_memory_with_zero_byte;
}

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}

namespace GQL
{

void planWhereClause(
    QueryPlan & plan,
    const OPENGQL::AST::GQLWhereClause & where,
    ContextPtr context,
    std::string_view context_name,
    const PlanScope & scope);

namespace AggregationPlannerDetail
{

namespace GAST = DB::OPENGQL::AST;

struct AggregateFunctionInfo
{
    String name;
    std::vector<const IAST *> arguments;
    bool count_star = false;
    bool bare_keyword = false;
    bool distinct = false;
};

inline String normalizedName(String name)
{
    trim(name);
    return Poco::toUpper(name);
}

inline bool isAggregateFunctionName(const String & name)
{
    const auto upper_name = normalizedName(name);
    return upper_name == "COUNT" || upper_name == "SUM" || upper_name == "AVG" || upper_name == "MIN" || upper_name == "MAX"
        || upper_name == "COLLECT_LIST" || upper_name == "STDDEV_SAMP" || upper_name == "STDDEV_POP";
}

inline const char * getAggregateFunctionName(const String & name)
{
    const auto upper_name = normalizedName(name);
    if (upper_name == "COUNT")
        return "count";
    if (upper_name == "SUM")
        return "sum";
    if (upper_name == "AVG")
        return "avg";
    if (upper_name == "MIN")
        return "min";
    if (upper_name == "MAX")
        return "max";
    if (upper_name == "COLLECT_LIST")
        return "groupArray";
    if (upper_name == "STDDEV_SAMP")
        return "stddevSamp";
    if (upper_name == "STDDEV_POP")
        return "stddevPop";

    return nullptr;
}

inline bool isGQLCountStar(const GAST::GQLExpr & aggregate)
{
    if (normalizedName(aggregate.text) != "COUNT" || aggregate.children.size() != 1)
        return false;

    const auto * argument = aggregate.children.front() ? aggregate.children.front()->as<GAST::GQLExpr>() : nullptr;
    return argument && argument->kind == GAST::GQLExpr::Kind::Literal && argument->text == "*";
}

inline std::optional<AggregateFunctionInfo> getAggregateFunctionInfo(const IAST & ast)
{
    if (const auto * expr = ast.as<GAST::GQLExpr>())
    {
        if (expr->kind != GAST::GQLExpr::Kind::FunctionCall || !isAggregateFunctionName(expr->text))
            return std::nullopt;

        AggregateFunctionInfo result;
        result.name = expr->text;
        result.count_star = isGQLCountStar(*expr);
        result.bare_keyword = expr->bare_keyword;
        result.distinct = expr->set_quantifier == GAST::GQLExpr::SetQuantifier::Distinct;

        if (!result.count_star)
        {
            result.arguments.reserve(expr->children.size());
            for (const auto & argument : expr->children)
            {
                if (!argument)
                    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Aggregate function '{}' has a null argument", expr->text);

                result.arguments.push_back(argument.get());
            }
        }

        return result;
    }

    if (const auto * function = ast.as<ASTFunction>())
    {
        if (!isAggregateFunctionName(function->name))
            return std::nullopt;

        if (function->parameters)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Parametric aggregate function '{}' is not supported", function->name);
        if (function->isWindowFunction())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Window aggregate function '{}' is not supported", function->name);
        if (function->isLambdaFunction())
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Lambda aggregate function '{}' is not supported", function->name);

        AggregateFunctionInfo result;
        result.name = function->name;

        const auto * expression_list = function->arguments ? function->arguments->as<ASTExpressionList>() : nullptr;
        if (function->arguments && !expression_list)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Aggregate function '{}' arguments must be ASTExpressionList", function->name);

        const bool has_no_arguments = !expression_list || expression_list->children.empty();
        result.count_star = normalizedName(function->name) == "COUNT" && has_no_arguments;
        if (has_no_arguments && !result.count_star)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Aggregate function '{}' must have at least one argument", function->name);

        if (!result.count_star)
        {
            result.arguments.reserve(expression_list->children.size());
            for (const auto & argument : expression_list->children)
            {
                if (!argument)
                    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Aggregate function '{}' has a null argument", function->name);

                result.arguments.push_back(argument.get());
            }
        }

        return result;
    }

    return std::nullopt;
}

inline bool isAggregateProjectionItem(const ASTPtr & item_ast)
{
    const auto * item = item_ast ? item_ast->as<GAST::GQLAliasedItem>() : nullptr;
    return item && item->expression && getAggregateFunctionInfo(*item->expression).has_value();
}

inline String getOutputName(const GAST::GQLAliasedItem & item, size_t index, std::string_view context_name)
{
    if (!item.alias.empty())
        return item.alias;
    if (item.expression)
    {
        const auto name = item.expression->getColumnName();
        if (!name.empty())
            return name;
    }

    return fmt::format("__gql_{}_{}", context_name, index);
}

inline Names extractGroupByKeys(const GAST::GQLGroupByClause * group_by)
{
    Names keys;
    if (!group_by || group_by->empty_grouping_set)
        return keys;

    keys.reserve(group_by->items.size());
    for (const auto & item_ast : group_by->items)
    {
        if (const auto * item = item_ast ? item_ast->as<GAST::GQLExpr>() : nullptr)
        {
            if (item->kind != GAST::GQLExpr::Kind::Identifier || item->text.empty())
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Only binding-variable identifiers are supported in GQL GROUP BY");

            keys.push_back(item->text);
            continue;
        }

        if (const auto * identifier = item_ast ? item_ast->as<ASTIdentifier>() : nullptr)
        {
            keys.push_back(identifier->name());
            continue;
        }

        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Only binding-variable identifiers are supported in GQL GROUP BY");
    }

    return keys;
}

inline String makeUniqueDAGOutputName(const ActionsDAG & dag, const String & prefix, size_t index)
{
    String name = fmt::format("{}_{}", prefix, index);
    while (dag.tryFindInOutputs(name))
    {
        ++index;
        name = fmt::format("{}_{}", prefix, index);
    }

    return name;
}

inline const ActionsDAG::Node & materializeAggregateArgument(
    ActionsDAG & dag,
    const ActionsDAG::Node & node,
    const String & prefix,
    size_t index)
{
    if (dag.tryFindInOutputs(node.result_name))
        return node;

    const auto & alias = dag.addAlias(node, makeUniqueDAGOutputName(dag, prefix, index));
    dag.getOutputs().push_back(&alias);
    return alias;
}

inline AggregateDescription makeAggregateDescription(
    const AggregateFunctionInfo & aggregate,
    const String & output_name,
    ActionsDAG & dag,
    ContextPtr context,
    const PlanScope & scope,
    size_t aggregate_index)
{
    if (aggregate.bare_keyword)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Bare keyword aggregate function '{}' is not supported", aggregate.name);
    if (aggregate.distinct)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "DISTINCT aggregate function '{}' is not supported", aggregate.name);

    const auto * function_name = getAggregateFunctionName(aggregate.name);
    if (!function_name)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Aggregate function '{}' is not supported", aggregate.name);
    if (!aggregate.count_star && aggregate.arguments.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Aggregate function '{}' must have at least one argument", aggregate.name);

    AggregateDescription description;
    description.column_name = output_name;

    DataTypes argument_types;
    if (!aggregate.count_star)
    {
        description.argument_names.reserve(aggregate.arguments.size());
        argument_types.reserve(aggregate.arguments.size());
        for (size_t argument_index = 0; argument_index < aggregate.arguments.size(); ++argument_index)
        {
            const auto & argument = buildExpressionNode(*aggregate.arguments[argument_index], dag, context, scope);
            const auto & materialized_argument = materializeAggregateArgument(
                dag,
                argument,
                fmt::format("__gql_agg_arg_{}", aggregate_index),
                argument_index);
            description.argument_names.push_back(materialized_argument.result_name);
            argument_types.push_back(materialized_argument.result_type);
        }
    }

    AggregateFunctionProperties properties;
    description.function = AggregateFunctionFactory::instance().get(
        function_name,
        NullsAction::EMPTY,
        argument_types,
        description.parameters,
        properties);
    return description;
}

inline Aggregator::Params makeAggregatorParams(const Names & keys, const AggregateDescriptions & aggregates, ContextPtr context)
{
    const auto & settings = context->getSettingsRef();
    const bool empty_result_for_aggregation_by_empty_set = settings[Setting::empty_result_for_aggregation_by_empty_set]
        || (settings[Setting::empty_result_for_aggregation_by_constant_keys_on_empty_set] && keys.empty());

    return Aggregator::Params{
        keys,
        aggregates,
        false,
        settings[Setting::max_rows_to_group_by],
        settings[Setting::group_by_overflow_mode],
        settings[Setting::group_by_two_level_threshold],
        settings[Setting::group_by_two_level_threshold_bytes],
        Aggregator::Params::getMaxBytesBeforeExternalGroupBy(
            settings[Setting::max_bytes_before_external_group_by],
            settings[Setting::max_bytes_ratio_before_external_group_by]),
        empty_result_for_aggregation_by_empty_set,
        context->getTempDataOnDisk(),
        settings[Setting::max_threads],
        settings[Setting::min_free_disk_space_for_temporary_data],
        settings[Setting::compile_aggregate_expressions],
        settings[Setting::min_count_to_compile_aggregate_expression],
        settings[Setting::max_block_size],
        settings[Setting::enable_software_prefetch_in_aggregation],
        false,
        settings[Setting::optimize_group_by_constant_keys],
        settings[Setting::min_hit_rate_to_use_consecutive_keys_optimization],
        StatsCollectingParams{},
        settings[Setting::enable_producing_buckets_out_of_order_in_aggregation],
        settings[Setting::serialize_string_in_memory_with_zero_byte]};
}

inline void addAggregatingStep(QueryPlan & plan, Names keys, AggregateDescriptions aggregates, ContextPtr context)
{
    const auto & settings = context->getSettingsRef();
    auto params = makeAggregatorParams(keys, aggregates, context);

    plan.addStep(std::make_unique<AggregatingStep>(
        plan.getCurrentHeader(),
        std::move(params),
        GroupingSetsParamsList{},
        true,
        settings[Setting::max_block_size],
        settings[Setting::aggregation_in_order_max_block_bytes],
        settings[Setting::max_threads],
        settings[Setting::max_threads],
        false,
        settings[Setting::group_by_use_nulls],
        SortDescription{},
        SortDescription{},
        false,
        false,
        false));
}

}

inline bool hasAggregateProjectionItems(const ASTs & items)
{
    for (const auto & item : items)
    {
        if (AggregationPlannerDetail::isAggregateProjectionItem(item))
            return true;
    }

    return false;
}

inline void planAggregatingProjectionItems(
    QueryPlan & plan,
    const ASTs & items,
    const OPENGQL::AST::GQLGroupByClause * group_by,
    const OPENGQL::AST::GQLWhereClause * having,
    ContextPtr context,
    std::string_view context_name,
    PlanScope & scope)
{
    namespace Detail = AggregationPlannerDetail;

    if (items.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "{} must project at least one item", context_name);

    Names keys = Detail::extractGroupByKeys(group_by);
    AggregateDescriptions aggregates;
    aggregates.reserve(items.size());

    auto current_header = plan.getCurrentHeader();
    ActionsDAG before_aggregation(current_header->getColumnsWithTypeAndName());
    std::vector<String> aggregate_output_names(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto * item = items[i] ? items[i]->as<OPENGQL::AST::GQLAliasedItem>() : nullptr;
        if (!item)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL {} item must be GQLAliasedItem", context_name);
        if (!item->expression)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL {} item must wrap an expression", context_name);

        auto aggregate = Detail::getAggregateFunctionInfo(*item->expression);
        if (!aggregate)
            continue;

        aggregate_output_names[i] = Detail::getOutputName(*item, i, context_name);
        aggregates.push_back(Detail::makeAggregateDescription(*aggregate, aggregate_output_names[i], before_aggregation, context, scope, i));
    }

    if (aggregates.empty() && keys.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "{} aggregation requires GROUP BY or aggregate functions", context_name);

    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(before_aggregation)));
    Detail::addAggregatingStep(plan, keys, aggregates, context);
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);

    if (having)
        planWhereClause(plan, *having, context, context_name, scope);

    current_header = plan.getCurrentHeader();
    ActionsDAG projection(current_header->getColumnsWithTypeAndName());
    ActionsDAG::NodeRawConstPtrs outputs;
    outputs.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto * item = items[i]->as<OPENGQL::AST::GQLAliasedItem>();
        auto aggregate = Detail::getAggregateFunctionInfo(*item->expression);
        if (aggregate)
        {
            const auto * node = projection.tryFindInOutputs(aggregate_output_names[i]);
            if (!node)
                throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL aggregate output '{}' is missing", aggregate_output_names[i]);

            outputs.push_back(node);
            continue;
        }

        const auto & node = buildExpressionNode(*item->expression, projection, context, scope);
        if (item->alias.empty())
            outputs.push_back(&node);
        else
            outputs.push_back(&projection.addAlias(node, item->alias));
    }

    projection.getOutputs() = std::move(outputs);
    plan.addStep(std::make_unique<ExpressionStep>(current_header, std::move(projection)));
    scope.replaceWithHeader(*plan.getCurrentHeader(), BindingKind::Projection);
}

}

}
