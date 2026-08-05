#pragma once

namespace DB
{
class QueryTreePassManager;

namespace GQL
{

/// Register the standard GQL analysis pipeline in the common QueryTree pass manager.
void addQueryTreePasses(QueryTreePassManager& manager);

}  // namespace GQL
}  // namespace DB
