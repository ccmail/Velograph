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
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/parseQuery.h>
#include <Processors/Executors/PushingPipelineExecutor.h>
#include <QueryPipeline/BlockIO.h>
#include <QueryPipeline/Pipe.h>
#include <Storages/IStorage.h>

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
