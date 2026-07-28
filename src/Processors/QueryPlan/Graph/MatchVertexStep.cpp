#include <Processors/QueryPlan/Graph/MatchVertexStep.h>

#include <Columns/ColumnsNumber.h>
#include <Common/Exception.h>
#include <Common/JSONBuilder.h>
#include <Core/Defines.h>
#include <DataTypes/DataTypesNumber.h>
#include <Interpreters/ExpressionActions.h>
#include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#include <Processors/Transforms/ExpressionTransform.h>
#include <QueryPipeline/Pipe.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/Graph/IGraphStorage.h>

namespace DB::ErrorCodes
{
extern const int LOGICAL_ERROR;
}

namespace DB::Graph
{

SharedHeader MatchVertexStep::makeHeader(
    const GraphStoragePtr & storage,
    const String & variable,
    const Names & referenced_columns)
{
    Block header;
    const auto & storage_header = storage->getGraphHeader(GraphElementKind::Vertex);
    const String prefix = variable + ".";

    for (const auto & column_name : referenced_columns)
    {
        String storage_name;
        if (column_name == variable)
            storage_name = String(GRAPH_COL_ID);
        else if (column_name.starts_with(prefix))
            storage_name = column_name.substr(prefix.size());
        else
            continue;

        if (!storage_header.has(storage_name))
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "Referenced GQL column '{}' is absent from the vertex storage header",
                column_name);

        header.insert({storage_header.getByName(storage_name).type, column_name});
    }

    return std::make_shared<const Block>(std::move(header));
}

MatchVertexStep::MatchVertexStep(
    GraphStoragePtr storage_,
    String variable_,
    Names referenced_columns_,
    ContextPtr context_)
    : SourceStepWithFilterBase(makeHeader(storage_, variable_, referenced_columns_))
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
    /// M2 scans all vertices with eager property projection. Predicate pushdown
    /// remains intentionally disabled until M3; filters execute above this source.
    NameSet storage_projection;
    const String prefix = variable + ".";
    for (const auto & column_name : getOutputHeader()->getNames())
    {
        storage_projection.insert(
            column_name == variable ? String(GRAPH_COL_ID) : column_name.substr(prefix.size()));
    }

    auto pipe = storage->scan(
        storage_projection,
        GraphElementKind::Vertex,
        DEFAULT_BLOCK_SIZE,
        /*num_streams=*/1);

    /// Rename storage columns into plan-layer names and preserve the deterministic
    /// id-first, property-name order from the output header.
    ActionsDAG dag(pipe.getHeader().getColumnsWithTypeAndName(), false);
    ActionsDAG::NodeRawConstPtrs outputs;
    outputs.reserve(getOutputHeader()->columns());
    for (const auto & output_column : *getOutputHeader())
    {
        const auto storage_name
            = output_column.name == variable ? String(GRAPH_COL_ID) : output_column.name.substr(prefix.size());
        const auto * input = dag.tryFindInOutputs(storage_name);
        if (!input)
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "Graph vertex scan did not return projected column '{}'",
                storage_name);
        outputs.push_back(&dag.addAlias(*input, output_column.name));
    }
    dag.getOutputs() = std::move(outputs);

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
