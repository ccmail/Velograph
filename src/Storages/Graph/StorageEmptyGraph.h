#pragma once

#include <Storages/Graph/IGraphStorage.h>

namespace DB
{

/** Fail-closed placeholder graph storage.
  *
  * Produces no rows for any graph primitive, and inherits the fail-closed
  * `IStorage::write`. It exists so the graph source/storage contract is
  * concrete, instantiable, and testable before a real graph storage engine is
  * implemented.
  */
class StorageEmptyGraph final : public IGraphStorage
{
public:
    using IGraphStorage::IGraphStorage;

    std::string getName() const override { return "EmptyGraph"; }

    Pipe scan(
        const SharedHeader & header,
        const IColumn::Filter & header_filter,
        GraphElementKind kind,
        size_t max_block_size,
        size_t num_streams) override;
};

}
