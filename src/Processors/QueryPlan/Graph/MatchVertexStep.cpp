#include <Processors/QueryPlan/Graph/MatchVertexStep.h>

#include <Columns/ColumnsNumber.h>
#include <Common/JSONBuilder.h>
#include <Core/Defines.h>
#include <DataTypes/DataTypesNumber.h>
#include <Interpreters/ExpressionActions.h>
#include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#include <Processors/Transforms/ExpressionTransform.h>
#include <QueryPipeline/Pipe.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/Graph/IGraphStorage.h>

namespace DB::Graph
{

SharedHeader MatchVertexStep::makeHeader(const String & variable)
{
    /// M1: the vertex step outputs only the variable's id column as UInt64.
    /// Property columns (a.age, etc.) are added in M2.
    Block header;
    header.insert({ColumnUInt64::create(), std::make_shared<DataTypeUInt64>(), variable});
    return std::make_shared<const Block>(std::move(header));
}

MatchVertexStep::MatchVertexStep(
    GraphStoragePtr storage_,
    String variable_,
    Names referenced_columns_,
    ContextPtr context_)
    : SourceStepWithFilterBase(makeHeader(variable_))
    , storage(std::move(storage_))
    , variable(std::move(variable_))
    , referenced_columns(std::move(referenced_columns_))
    , context(std::move(context_))
{
    setStepDescription("GraphMatchVertex");
}

QueryPlanStepPtr MatchVertexStep::clone() const
{
    return std::make_unique<MatchVertexStep>(storage, variable, referenced_columns, context);
}

void MatchVertexStep::initializePipeline(QueryPipelineBuilder & pipeline, const BuildQueryPipelineSettings &)
{
    /// M1: scan all vertices, project __ID__, rename to the variable name.
    /// Predicate pushdown (applyFilters) is not wired in M1; the filter DAG
    /// accumulated by the base class is accepted but ignored.
    NameSet storage_projection;
    storage_projection.insert(String(GRAPH_COL_ID));

    auto pipe = storage->scan(
        storage_projection,
        GraphElementKind::Vertex,
        DEFAULT_BLOCK_SIZE,
        /*num_streams=*/1);

    /// Rename __ID__ → variable using a converting expression.
    auto dag = ActionsDAG::makeConvertingActions(
        pipe.getHeader().getColumnsWithTypeAndName(),
        getOutputHeader()->getColumnsWithTypeAndName(),
        ActionsDAG::MatchColumnsMode::Position,
        context);

    auto expression = std::make_shared<ExpressionActions>(std::move(dag));
    pipe.addSimpleTransform(
        [&](const SharedHeader & header)
        {
            return std::make_shared<ExpressionTransform>(header, expression);
        });

    pipeline.init(std::move(pipe));
}

void MatchVertexStep::describeActions(FormatSettings & format_settings) const
{
    const String desc = "variable=" + variable;
    format_settings.out.write(desc.data(), desc.size());
}

void MatchVertexStep::describeActions(JSONBuilder::JSONMap & map) const
{
    map.add("variable", variable);
}

}
