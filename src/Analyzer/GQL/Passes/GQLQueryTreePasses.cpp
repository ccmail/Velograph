#include <Analyzer/GQL/Passes/GQLQueryTreePasses.h>

#include <Analyzer/GQL/Passes/GQLNameResolutionPass.h>
#include <Analyzer/GQL/Passes/GQLPredicateNormalizationPass.h>
#include <Analyzer/QueryTreePassManager.h>

#include <memory>

namespace DB::GQL
{

void addQueryTreePasses(QueryTreePassManager& manager)
{
    manager.addResolvePass(std::make_unique<GQLNameResolutionPass>());
    manager.addResolvePass(std::make_unique<GQLPredicateNormalizationPass>());
}

}  // namespace DB::GQL
