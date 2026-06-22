#include <Storages/Graph/GraphStorageEngine.h>

#include <Columns/ColumnArray.h>
#include <Columns/ColumnsNumber.h>
#include <Columns/ColumnString.h>
#include <Common/Exception.h>
#include <Common/logger_useful.h>
#include <Core/Block.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>
#include <Interpreters/Context.h>
#include <Interpreters/InterpreterCreateQuery.h>
#include <Interpreters/InterpreterDropQuery.h>
#include <Interpreters/InterpreterInsertQuery.h>
#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTDropQuery.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTInsertQuery.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTSelectWithUnionQuery.h>
#include <Parsers/ASTTablesInSelectQuery.h>
#include <Parsers/parseQuery.h>
#include <Parsers/ParserCreateQuery.h>
#include <Processors/Executors/PullingPipelineExecutor.h>
#include <QueryPipeline/BlockIO.h>
#include <QueryPipeline/Pipe.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/IStorage.h>
#include <Storages/MergeTree/StorageMergeTree.h>
#include <Storages/StorageFactory.h>

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

Pipe GraphStorageEngine::scan(
    const SharedHeader & /*header*/,
    const IColumn::Filter & /*header_filter*/,
    GraphElementKind /*kind*/,
    size_t /*max_block_size*/,
    size_t /*num_streams*/)
{
    /// TODO(graph-storage): route to `vertices` or `edges_forward` / `edges_reverse`
    /// depending on `kind`. Until then fail closed so the planner does not
    /// silently read empty data.
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GraphStorageEngine::scan is not implemented yet");
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
    /// Build CREATE TABLE AST programmatically
    auto create_query = std::make_shared<ASTCreateQuery>();

    size_t dot_pos = table_name.find('.');
    if (dot_pos != String::npos)
    {
        create_query->database = table_name.substr(0, dot_pos);
        create_query->table = table_name.substr(dot_pos + 1);
    }
    else
    {
        create_query->table = table_name;
    }

    create_query->set(create_query->storage, std::make_shared<ASTStorage>());

    /// `ReplacingMergeTree` takes the version column as its first engine argument so
    /// that duplicate rows are collapsed by `__RANK__` during compaction.
    if (engine == "ReplacingMergeTree")
        create_query->storage->set(create_query->storage->engine, makeASTFunction(engine, makeASTIdentifier(COL_RANK)));
    else
        create_query->storage->set(create_query->storage->engine, makeASTFunction(engine));

    /// Columns
    auto columns_ast = std::make_shared<ASTExpressionList>();
    for (const auto & col : columns)
    {
        auto col_decl = std::make_shared<ASTColumnDeclaration>();
        col_decl->name = col.name;
        col_decl->type = col.type->getDefaultAST();
        columns_ast->children.push_back(col_decl);
    }

    auto columns_list = std::make_shared<ASTColumns>();
    columns_list->set(columns_list->columns, columns_ast);
    create_query->set(create_query->columns_list, columns_list);

    /// ORDER BY
    if (!order_by.empty())
    {
        auto order_by_ast = std::make_shared<ASTOrderByExpressionList>();
        auto parts = String(order_by).split(",");
        for (const auto & part : parts)
        {
            String trimmed = part;
            trimmed.trim();
            if (!trimmed.empty())
                order_by_ast->children.push_back(std::make_shared<ASTIdentifier>(trimmed));
        }
        create_query->storage->set(create_query->storage->order_by, order_by_ast);
    }

    /// Materialized view source
    if (is_materialized_view)
    {
        /// Mark the statement as a `CREATE MATERIALIZED VIEW` so the create interpreter
        /// treats it as a view-with-inner-table (no `TO` target): the MV itself owns the
        /// SummingMergeTree storage and is populated automatically on inserts into the
        /// source edge table.
        create_query->is_materialized_view = true;
        create_query->as_select = true;
        /// Parse the SELECT query for the MV
        ParserSelectWithUnionQuery parser;
        String select_sql = mv_select;
        const char * begin = select_sql.data();
        const char * end = begin + select_sql.size();
        auto select_ast = parseQuery(parser, begin, end, "", 0, 0);
        create_query->set(create_query->select, select_ast);
    }

    /// Execute via InterpreterCreateQuery
    InterpreterCreateQuery interpreter(create_query, context);
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
    auto storage = getInternalStorage(verticesTableName());

    auto insert_query = std::make_shared<ASTInsertQuery>();
    insert_query->database = graph_name;
    insert_query->table = "vertices";

    InterpreterInsertQuery interpreter(insert_query, context, true /*allow_materialized*/);
    auto block_io = interpreter.execute();
    auto & pipeline = block_io.pipeline;

    pipeline.push(block);
    pipeline.finish();
}

void GraphStorageEngine::writeEdge(const Block & block)
{
    /// Write to forward table
    {
        auto insert_query = std::make_shared<ASTInsertQuery>();
        insert_query->database = graph_name;
        insert_query->table = "edges_forward";

        InterpreterInsertQuery interpreter(insert_query, context, true);
        auto block_io = interpreter.execute();

        block_io.pipeline.push(block);
        block_io.pipeline.finish();
    }

    /// Write to reverse table (swap __SRC__ and __DST__)
    {
        /// Build a reversed block
        Block reversed_block = block.cloneEmpty();

        size_t src_pos = block.getPositionByName("__SRC__");
        size_t dst_pos = block.getPositionByName("__DST__");

        for (size_t i = 0; i < block.columns(); ++i)
        {
            const auto & col = block.getByPosition(i);
            if (col.name == "__SRC__")
                reversed_block.getByPosition(i).column = block.getByPosition(dst_pos).column;
            else if (col.name == "__DST__")
                reversed_block.getByPosition(i).column = block.getByPosition(src_pos).column;
            else
                reversed_block.getByPosition(i).column = col.column;
        }

        auto insert_query = std::make_shared<ASTInsertQuery>();
        insert_query->database = graph_name;
        insert_query->table = "edges_reverse";

        InterpreterInsertQuery interpreter(insert_query, context, true);
        auto block_io = interpreter.execute();

        block_io.pipeline.push(reversed_block);
        block_io.pipeline.finish();
    }
}

// --- Read operations ---

Pipe GraphStorageEngine::readForwardEdges(UInt64 src, const String & edge_type, ContextPtr read_context) const
{
    (void)src;
    (void)edge_type;

    auto storage = getInternalStorage(edgesForwardTableName());

    /// TODO(graph-storage): push the `__SRC__ = src` (and optional `type = edge_type`)
    /// predicate down into the MergeTree reader so the storage layer skips irrelevant
    /// granules. The intended path is to build a `SELECT ... WHERE __SRC__ = N` AST,
    /// parse it with `ParserSelectWithUnionQuery`, and run it through
    /// `InterpreterSelectWithUnionQuery::buildQueryPlan`, which lets the MergeTree
    /// optimizer extract a `KeyCondition` from the WHERE clause automatically. Direct
    /// part-level reading (a la `ReadFromMergeTree`) is a later performance step.
    /// Until then this scans the whole table, so it is correctness-only and not a real
    /// traversal primitive.

    auto snapshot = storage->getStorageSnapshot(read_context);
    SelectQueryInfo query_info;
    query_info.query = std::make_shared<ASTSelectQuery>();

    /// Get all columns
    Names column_names;
    for (const auto & col : storage->getInMemoryMetadataPtr()->getColumns().getAll())
        column_names.push_back(col.name);

    QueryPlan plan;
    storage->read(
        plan,
        column_names,
        snapshot,
        query_info,
        read_context,
        QueryProcessingStage::Complete,
        8192, /// max_block_size
        1); /// num_streams

    return *plan.buildQueryPipeline(
        QueryPlanOptimizationSettings(read_context),
        BuildQueryPipelineSettings(read_context));
}

Pipe GraphStorageEngine::readReverseEdges(UInt64 dst, const String & edge_type, ContextPtr read_context) const
{
    (void)dst;
    (void)edge_type;

    auto storage = getInternalStorage(edgesReverseTableName());

    /// TODO(graph-storage): push `__DST__ = dst` (and optional `type = edge_type`)
    /// down via `InterpreterSelectWithUnionQuery` (see `readForwardEdges`). Until then
    /// this scans the whole reverse-edge table.

    auto snapshot = storage->getStorageSnapshot(read_context);
    SelectQueryInfo query_info;

    Names column_names;
    for (const auto & col : storage->getInMemoryMetadataPtr()->getColumns().getAll())
        column_names.push_back(col.name);

    QueryPlan plan;
    storage->read(plan, column_names, snapshot, query_info, read_context, QueryProcessingStage::Complete, 8192, 1);

    return *plan.buildQueryPipeline(
        QueryPlanOptimizationSettings(read_context),
        BuildQueryPipelineSettings(read_context));
}

Block GraphStorageEngine::readVertex(UInt64 id, ContextPtr read_context) const
{
    (void)id;

    auto storage = getInternalStorage(verticesTableName());

    /// TODO(graph-storage): push `__ID__ = id` down via
    /// `InterpreterSelectWithUnionQuery` and return the single matched block (empty
    /// block if absent). Until then this scans the whole vertices table and returns an
    /// empty-shaped block.

    auto snapshot = storage->getStorageSnapshot(read_context);
    SelectQueryInfo query_info;

    Names column_names;
    for (const auto & col : storage->getInMemoryMetadataPtr()->getColumns().getAll())
        column_names.push_back(col.name);

    QueryPlan plan;
    storage->read(plan, column_names, snapshot, query_info, read_context, QueryProcessingStage::Complete, 8192, 1);

    auto pipeline = plan.buildQueryPipeline(
        QueryPlanOptimizationSettings(read_context),
        BuildQueryPipelineSettings(read_context));

    auto executor = std::make_unique<PullingPipelineExecutor>(*pipeline);
    Block result;
    Block block;
    while (executor->pull(block))
    {
        if (!result)
            result = block.cloneEmpty();
    }
    return result;
}

UInt64 GraphStorageEngine::getDegree(UInt64 src, const String & edge_type, ContextPtr read_context) const
{
    (void)src;
    (void)edge_type;
    (void)read_context;

    /// TODO(graph-storage): query the `vertex_degrees` SummingMergeTree for
    /// `SELECT sum(out_degree) FROM <graph>.vertex_degrees WHERE __SRC__ = N AND type = 'X'`.
    /// `sum()` is required because compaction may not have merged partial rows yet;
    /// the degree is eventually consistent (see design doc section 4.3).
    return 0;
}

} // namespace DB
