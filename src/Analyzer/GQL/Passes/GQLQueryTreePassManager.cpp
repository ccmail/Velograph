#include <Analyzer/GQL/Passes/GQLQueryTreePassManager.h>

#include <Analyzer/GQL/Passes/GQLNameResolutionPass.h>
#include <Analyzer/GQL/Passes/GQLPredicateNormalizationPass.h>

#include <Analyzer/ColumnNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/InDepthQueryTreeVisitor.h>
#include <Common/Exception.h>

#include <base/defines.h>

#include <utility>

namespace DB
{

namespace ErrorCodes
{
extern const int BAD_ARGUMENTS;
extern const int LOGICAL_ERROR;
}

namespace GQL
{

#if defined(DEBUG_OR_SANITIZER_BUILD)
namespace
{

/// Validates GQL QueryTree invariants after each pass (debug builds only): every ColumnNode
/// must have a source, and every FunctionNode must be resolved. This mirrors the SQL pass
/// validator, restricted to the invariants the GQL analyzer is responsible for.
class GQLValidationChecker : public ConstInDepthQueryTreeVisitor<GQLValidationChecker>
{
public:
    explicit GQLValidationChecker(String pass_name_) : pass_name(std::move(pass_name_)) { }

    void visitImpl(const QueryTreeNodePtr & node)
    {
        if (const auto * column = node->as<ColumnNode>())
        {
            if (!column->getColumnSourceOrNull())
                throw Exception(
                    ErrorCodes::LOGICAL_ERROR,
                    "GQL ColumnNode '{}' has no source after pass '{}'",
                    column->getColumnName(),
                    pass_name);
        }
        else if (const auto * function = node->as<FunctionNode>())
        {
            if (!function->isResolved())
                throw Exception(
                    ErrorCodes::LOGICAL_ERROR,
                    "GQL FunctionNode '{}' is not resolved after pass '{}'",
                    function->getFunctionName(),
                    pass_name);
        }
    }

private:
    String pass_name;
};

void validateQueryTree(const QueryTreeNodePtr & query_tree_node, const String & pass_name)
{
    GQLValidationChecker checker(pass_name);
    checker.visit(query_tree_node);
}

}
#endif

GQLQueryTreePassManager::GQLQueryTreePassManager(ContextPtr context_) : context(std::move(context_))
{
}

void GQLQueryTreePassManager::addPass(QueryTreePassPtr pass)
{
    passes.push_back(std::move(pass));
}

void GQLQueryTreePassManager::run(QueryTreeNodePtr & query_tree_node)
{
    run(query_tree_node, passes.size());
}

void GQLQueryTreePassManager::run(QueryTreeNodePtr & query_tree_node, size_t up_to_pass_index)
{
    if (up_to_pass_index > passes.size())
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS, "GQL pass index {} exceeds passes count {}", up_to_pass_index, passes.size());

    for (size_t i = 0; i < up_to_pass_index; ++i)
    {
        passes[i]->run(query_tree_node, context);
#if defined(DEBUG_OR_SANITIZER_BUILD)
        validateQueryTree(query_tree_node, passes[i]->getName());
#endif
    }
}

void GQLQueryTreePassManager::runOnlyResolve(QueryTreeNodePtr & query_tree_node)
{
    run(query_tree_node, resolution_passes_count);
}

void GQLQueryTreePassManager::addDefaultPasses(GQLQueryTreePassManager & manager)
{
    /// Resolution passes: GQL name + function resolution. They must not change the output
    /// header, and are exactly the passes runOnlyResolve runs.
    manager.addPass(std::make_unique<GQLNameResolutionPass>());
    manager.addPass(std::make_unique<GQLPredicateNormalizationPass>());
    manager.resolution_passes_count = manager.passes.size();

    /// Optimization passes would be appended here; they are skipped by runOnlyResolve.
}

}

}
