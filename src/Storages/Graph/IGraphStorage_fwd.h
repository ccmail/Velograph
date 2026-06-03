#pragma once

#include <memory>

namespace DB
{

class IGraphStorage;
using GraphStoragePtr = std::shared_ptr<IGraphStorage>;

}
