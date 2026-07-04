#include <Storages/StorageFactory.h>
#include <Storages/Graph/GraphStorageEngine.h>

namespace DB
{

void registerStorageGraphStorage(StorageFactory & factory)
{
    factory.registerStorage(
        "GraphStorage",
        [](const StorageFactory::Arguments & args) -> StoragePtr
        {
            /// The graph name is the database name. The table name is always
            /// `_graph`. Internal MergeTree tables are created on startup.
            auto context = args.getContext();
            auto storage = std::make_shared<GraphStorageEngine>(
                args.table_id.database_name, context);
            storage->createInternalTables();
            return storage;
        });
}

}
