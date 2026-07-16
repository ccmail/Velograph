#include <Storages/Graph/GraphStorageEngine.h>

#include <Common/Exception.h>
#include <Common/logger_useful.h>
#include <Core/Block.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <Interpreters/Context.h>
#include <Interpreters/DatabaseCatalog.h>
#include <Interpreters/InterpreterCreateQuery.h>
#include <Interpreters/InterpreterDropQuery.h>
#include <Interpreters/InterpreterInsertQuery.h>
#include <Parsers/ASTDropQuery.h>
#include <Parsers/ASTInsertQuery.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/parseQuery.h>
#include <Processors/Executors/PushingPipelineExecutor.h>
#include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#include <Processors/QueryPlan/Optimizations/QueryPlanOptimizationSettings.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <QueryPipeline/BlockIO.h>
#include <QueryPipeline/Pipe.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/IStorage.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <Storages/MergeTree/MergeTreeDataSelectExecutor.h>
#include <Storages/SelectQueryInfo.h>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int UNKNOWN_TABLE;
extern const int NOT_IMPLEMENTED;
} // namespace ErrorCodes

GraphStorageEngine::GraphStorageEngine(String graph_name_, ContextMutablePtr context_)
    : IGraphStorage(StorageID{graph_name_, "_graph"})
    , graph_name(std::move(graph_name_))
    , context(std::move(context_))
{
}

// --- IGraphStorage overrides ---

const Block & GraphStorageEngine::getGraphHeader(GraphElementKind kind) const
{
    auto build = [](Block & header, const NamesAndTypes & columns)
    {
        if (header.columns() > 0)
            return;
        for (const auto & col : columns)
            header.insert({col.type, col.name});
    };

    switch (kind)
    {
        case GraphElementKind::Vertex:
            build(vertex_header, schema.getVerticesTableColumns());
            return vertex_header;
        case GraphElementKind::Edge:
            build(edge_header, schema.getEdgesTableColumns());
            return edge_header;
    }
}

// --- MergeTree read helpers ---

Names GraphStorageEngine::buildColumnNames(const SharedHeader & header)
{
    Names names;
    names.reserve(header->columns());
    for (size_t i = 0; i < header->columns(); ++i)
        names.push_back(header->getByPosition(i).name);
    return names;
}

Pipe GraphStorageEngine::readFromInternalTable(
    const String & table_name,
    const Names & column_names,
    size_t max_block_size,
    size_t num_streams)
{
    auto storage = getInternalStorage(table_name);

    /// The internal tables are all MergeTree variants. We downcast to
    /// `MergeTreeData` to access the native select executor. This is safe
    /// because `createInternalTables` only creates MergeTree-engine tables.
    auto merge_tree = std::dynamic_pointer_cast<MergeTreeData>(storage);
    if (!merge_tree)
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "Internal table '{}' is not a MergeTree storage (got '{}')",
            table_name, storage->getName());

    auto metadata = merge_tree->getInMemoryMetadataPtr();
    auto snapshot = merge_tree->getStorageSnapshot(metadata, context);

    /// Construct an empty `SelectQueryInfo` — no SQL, no AST. The filter
    /// is empty so `read()` does a full scan with only column projection.
    /// Key-condition pushdown will be added by the `xxxImpl` overrides.
    /// A dummy `ASTSelectQuery` is needed because `MergeTreeDataSelectExecutor::read`
    /// dereferences `query_info.query` during analysis.
    SelectQueryInfo query_info;
    query_info.query = ASTPtr(new ASTSelectQuery());
    query_info.is_internal = true;

    MergeTreeDataSelectExecutor executor(*merge_tree);
    auto plan = executor.read(
        column_names, snapshot, query_info, context,
        max_block_size, num_streams);

    /// Disable plan optimization for internal reads: the `SelectQueryInfo`
    /// has a dummy AST, and optimization passes that expect a real query
    /// (e.g. filter pushdown, prewhere) must not run.
    QueryPlanResourceHolder resources;
    QueryPlanOptimizationSettings optimization_settings(context);
    optimization_settings.optimize_plan = false;
    auto builder = plan->buildQueryPipeline(
        optimization_settings, BuildQueryPipelineSettings{context});
    return QueryPipelineBuilder::getPipe(std::move(*builder), resources);
}

// --- Traversal primitives ---

Pipe GraphStorageEngine::scanImpl(
    const SharedHeader & header,
    const IColumn::Filter & /*header_filter*/,
    GraphElementKind kind,
    size_t max_block_size,
    size_t num_streams)
{
    /// Full scan: no key condition, only column projection. The column mask
    /// (`header_filter`) has already been resolved into `header` by the base
    /// class, so we just pass the requested column names to `read()`.
    auto table_name = (kind == GraphElementKind::Vertex)
        ? verticesTableName()
        : edgesForwardTableName();

    return readFromInternalTable(
        table_name, buildColumnNames(header), max_block_size, num_streams);
}

Pipe GraphStorageEngine::getVertexImpl(
    const SharedHeader & header,
    const IColumn::Filter & /*header_filter*/,
    const Columns & /*id_columns*/,
    size_t max_block_size,
    size_t num_streams)
{
    /// TODO(graph-storage): construct `KeyCondition` for `__ID__` IN (ids)
    /// and pass it through `SelectQueryInfo` so that `read()` skips irrelevant
    /// granules via the ORDER BY __ID__ mark index. For now, a full scan with
    /// column projection is sufficient to validate the end-to-end path.
    return readFromInternalTable(
        verticesTableName(), buildColumnNames(header), max_block_size, num_streams);
}

Pipe GraphStorageEngine::getEdgeImpl(
    const SharedHeader & header,
    const IColumn::Filter & /*header_filter*/,
    const Columns & /*edge_key_columns*/,
    size_t max_block_size,
    size_t num_streams)
{
    /// TODO(graph-storage): construct `KeyCondition` for the edge key
    /// (composite `__SRC__`/`type`/`__RANK__` or `__EID__`) to leverage
    /// mark skipping on the forward edge table.
    return readFromInternalTable(
        edgesForwardTableName(), buildColumnNames(header), max_block_size, num_streams);
}

Pipe GraphStorageEngine::getNeighborsImpl(
    const SharedHeader & header,
    const IColumn::Filter & /*header_filter*/,
    GraphDirection direction,
    const Columns & /*input_vertex_columns*/,
    size_t max_block_size,
    size_t num_streams,
    size_t /*limit_per_vertex*/,
    ASTPtr /*filter_pushdown*/)
{
    /// TODO(graph-storage): construct `KeyCondition` for `__SRC__` (FORWARD)
    /// or `__DST__` (BACKWARD) IN (vertex_ids) to leverage mark skipping.
    /// `limit_per_vertex` and `filter_pushdown` will be pushed into the
    /// reader as pre-where / row-level filters.
    auto column_names = buildColumnNames(header);

    switch (direction)
    {
        case GraphDirection::FORWARD:
            return readFromInternalTable(
                edgesForwardTableName(), column_names, max_block_size, num_streams);
        case GraphDirection::BACKWARD:
            return readFromInternalTable(
                edgesReverseTableName(), column_names, max_block_size, num_streams);
        case GraphDirection::UNDIRECTED:
        {
            /// Read both forward and reverse tables and union the output.
            /// The base class `getReturnHeaderForColumns` with `UNDIRECTED`
            /// does not swap src/dst, so both halves share the same column
            /// layout and can be united directly.
            auto fwd = readFromInternalTable(
                edgesForwardTableName(), column_names, max_block_size, num_streams);
            auto rev = readFromInternalTable(
                edgesReverseTableName(), column_names, max_block_size, num_streams);
            Pipes pipes;
            pipes.emplace_back(std::move(fwd));
            pipes.emplace_back(std::move(rev));
            return Pipe::unitePipes(std::move(pipes));
        }
    }

    throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown graph direction in getNeighborsImpl");
}

// --- Schema management ---

void GraphStorageEngine::registerVertexType(const String & type_name, const NamesAndTypes & columns)
{
    schema.registerVertexType(type_name, columns);
}

void GraphStorageEngine::registerEdgeType(
    const String & type_name,
    const NamesAndTypes & columns,
    const String & source_vertex_type,
    const String & target_vertex_type)
{
    schema.registerEdgeType(type_name, columns, source_vertex_type, target_vertex_type);
}

// --- Table naming ---

String GraphStorageEngine::verticesTableName() const
{
    return graph_name + ".vertices";
}

String GraphStorageEngine::edgesForwardTableName() const
{
    return graph_name + ".edges_forward";
}

String GraphStorageEngine::edgesReverseTableName() const
{
    return graph_name + ".edges_reverse";
}

String GraphStorageEngine::vertexDegreesTableName() const
{
    return graph_name + ".vertex_degrees";
}

// --- Internal table creation ---

void GraphStorageEngine::createTable(
    const String & table_name,
    const NamesAndTypes & columns,
    const String & order_by,
    const String & engine,
    bool is_materialized_view,
    const String & /*mv_source_table*/,
    const String & mv_select)
{
    /// Build the CREATE TABLE / CREATE MATERIALIZED VIEW statement as a SQL string
    /// and parse it, rather than constructing the AST programmatically. This avoids
    /// tight coupling to the internal AST layout (`ASTPtr` vs `std::shared_ptr`, column
    /// declaration field names, etc.) and lets the parser handle all syntax details.
    String sql;
    sql += is_materialized_view ? "CREATE MATERIALIZED VIEW " : "CREATE TABLE ";
    sql += table_name;
    sql += " (";
    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i > 0)
            sql += ", ";
        sql += columns[i].name + " " + columns[i].type->getName();
    }
    sql += ")";

    /// `ReplacingMergeTree` takes the version column as its first engine argument so
    /// that duplicate rows are collapsed by `__RANK__` during compaction.
    if (engine == "ReplacingMergeTree")
        sql += " ENGINE = " + engine + "(" + COL_RANK + ")";
    else
        sql += " ENGINE = " + engine;

    if (!order_by.empty())
        sql += " ORDER BY (" + order_by + ")";

    if (is_materialized_view && !mv_select.empty())
        sql += " AS " + mv_select;

    ParserCreateQuery parser;
    const char * begin = sql.data();
    const char * end = begin + sql.size();
    auto ast = parseQuery(parser, begin, end, "CREATE TABLE for graph storage", 0, 0, 0);

    InterpreterCreateQuery interpreter(ast, context);
    interpreter.setInternal(true);
    interpreter.execute();
}

void GraphStorageEngine::createInternalTables()
{
    if (isInitialized())
        return;

    /// 1. Vertices table
    auto vertex_columns = schema.getVerticesTableColumns();
    createTable(verticesTableName(), vertex_columns, "__ID__", "MergeTree");

    /// 2. Forward edges table
    auto edge_columns = schema.getEdgesTableColumns();
    createTable(edgesForwardTableName(), edge_columns, "__SRC__,type,__RANK__", "ReplacingMergeTree", false, "", "");

    /// 3. Reverse edges table (same columns but ORDER BY __DST__)
    createTable(edgesReverseTableName(), edge_columns, "__DST__,type,__RANK__", "ReplacingMergeTree", false, "", "");

    /// 4. Vertex degrees table (SummingMergeTree via MV)
    NamesAndTypes degree_columns;
    degree_columns.emplace_back("__SRC__", std::make_shared<DataTypeUInt64>());
    degree_columns.emplace_back("type", std::make_shared<DataTypeString>());
    degree_columns.emplace_back("out_degree", std::make_shared<DataTypeUInt64>());

    createTable(
        vertexDegreesTableName(),
        degree_columns,
        "__SRC__,type",
        "SummingMergeTree",
        true, /// is_materialized_view
        edgesForwardTableName(),
        "SELECT __SRC__, type, count() AS out_degree FROM " + edgesForwardTableName() + " GROUP BY __SRC__, type");
}

void GraphStorageEngine::dropInternalTables()
{
    /// Drop in dependency order: the degree MV first (so it stops reacting to edge
    /// inserts), then the reverse/forward edge tables, then the vertices table. Each
    /// drop goes through `InterpreterDropQuery::executeDropQuery` by `StorageID` rather
    /// than building a `DROP TABLE` AST, mirroring how `StorageMaterializedView` drops
    /// its inner table.
    auto drop_by_name = [this](const String & unqualified_name)
    {
        InterpreterDropQuery::executeDropQuery(
            ASTDropQuery::Kind::Drop,
            context,
            context,
            StorageID{graph_name, unqualified_name},
            /*sync=*/false);
    };

    drop_by_name("vertex_degrees");
    drop_by_name("edges_reverse");
    drop_by_name("edges_forward");
    drop_by_name("vertices");
}

bool GraphStorageEngine::isInitialized() const
{
    auto storage = DatabaseCatalog::instance().tryGetTable(StorageID{graph_name, "vertices"}, context);
    return storage != nullptr;
}

StoragePtr GraphStorageEngine::getInternalStorage(const String & table_name) const
{
    size_t dot_pos = table_name.find('.');
    String db = dot_pos != String::npos ? table_name.substr(0, dot_pos) : graph_name;
    String tbl = dot_pos != String::npos ? table_name.substr(dot_pos + 1) : table_name;

    auto storage = DatabaseCatalog::instance().tryGetTable(StorageID{db, tbl}, context);
    if (!storage)
        throw Exception(ErrorCodes::UNKNOWN_TABLE, "Internal graph table '{}' is not found", table_name);
    return storage;
}

// --- Write operations ---

void GraphStorageEngine::writeVertex(const Block & block)
{
    auto insert_query = make_intrusive<ASTInsertQuery>();
    insert_query->table_id = StorageID{graph_name, "vertices"};

    InterpreterInsertQuery interpreter(insert_query, context, true /*allow_materialized*/, false, false, false);
    auto block_io = interpreter.execute();

    PushingPipelineExecutor executor(block_io.pipeline);
    executor.start();
    executor.push(block);
    executor.finish();
}

void GraphStorageEngine::writeEdge(const Block & block)
{
    /// Write to forward table
    {
        auto insert_query = make_intrusive<ASTInsertQuery>();
        insert_query->table_id = StorageID{graph_name, "edges_forward"};

        InterpreterInsertQuery interpreter(insert_query, context, true, false, false, false);
        auto block_io = interpreter.execute();

        PushingPipelineExecutor executor(block_io.pipeline);
        executor.start();
        executor.push(block);
        executor.finish();
    }

    /// Write to reverse table (swap __SRC__ and __DST__)
    {
        /// Build a reversed block
        Block reversed_block = block.cloneEmpty();

        size_t src_pos = block.getPositionByName(COL_SRC);
        size_t dst_pos = block.getPositionByName(COL_DST);

        for (size_t i = 0; i < block.columns(); ++i)
        {
            const auto & col = block.getByPosition(i);
            if (col.name == COL_SRC)
                reversed_block.getByPosition(i).column = block.getByPosition(dst_pos).column;
            else if (col.name == COL_DST)
                reversed_block.getByPosition(i).column = block.getByPosition(src_pos).column;
            else
                reversed_block.getByPosition(i).column = col.column;
        }

        auto insert_query = make_intrusive<ASTInsertQuery>();
        insert_query->table_id = StorageID{graph_name, "edges_reverse"};

        InterpreterInsertQuery interpreter(insert_query, context, true, false, false, false);
        auto block_io = interpreter.execute();

        PushingPipelineExecutor executor(block_io.pipeline);
        executor.start();
        executor.push(reversed_block);
        executor.finish();
    }
}

} // namespace DB
