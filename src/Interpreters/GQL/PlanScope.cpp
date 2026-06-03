#include <Interpreters/GQL/PlanScope.h>

#include <Core/Block.h>
#include <Parsers/IAST.h>

#include <algorithm>
#include <utility>

namespace DB::GQL
{

void PlanScope::replaceWithHeader(const Block & header, BindingKind kind)
{
    std::vector<PlanBinding> preserved_expression_bindings;
    if (kind == BindingKind::Source)
    {
        for (const auto & binding : bindings)
        {
            if (!binding.expression)
                continue;

            preserved_expression_bindings.push_back(PlanBinding{
                .name = binding.name,
                .type = binding.type,
                .kind = binding.kind,
                .expression = binding.expression->clone(),
            });
        }
    }

    bindings.clear();
    bindings.reserve(header.columns() + preserved_expression_bindings.size());

    for (size_t i = 0; i < header.columns(); ++i)
    {
        const auto & column = header.getByPosition(i);
        addOrReplaceBinding(column.name, column.type, kind);
    }

    for (auto & binding : preserved_expression_bindings)
    {
        if (!hasBinding(binding.name))
            bindings.push_back(std::move(binding));
    }
}

bool PlanScope::hasBinding(const String & name) const
{
    return tryGetBinding(name) != nullptr;
}

const PlanBinding * PlanScope::tryGetBinding(const String & name) const
{
    const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const auto & binding)
    {
        return binding.name == name;
    });

    if (it == bindings.end())
        return nullptr;

    return &*it;
}

void PlanScope::addOrReplaceBinding(String name, DataTypePtr type, BindingKind kind, ASTPtr expression)
{
    if (name.empty())
        return;

    const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const auto & binding)
    {
        return binding.name == name;
    });

    if (it != bindings.end())
    {
        it->type = std::move(type);
        it->kind = kind;
        it->expression = std::move(expression);
        return;
    }

    bindings.push_back(PlanBinding{
        .name = std::move(name),
        .type = std::move(type),
        .kind = kind,
        .expression = std::move(expression),
    });
}

void PlanScope::setActiveGraph(ASTPtr graph_reference_)
{
    active_graph = std::move(graph_reference_);
}

PlanScope PlanScope::makeChildGraphScope() const
{
    PlanScope result;
    if (active_graph)
        result.setActiveGraph(active_graph->clone());

    return result;
}

PlanScope PlanScope::makeGraphOverrideScope(const ASTPtr & graph_reference) const
{
    auto result = *this;
    result.setActiveGraph(graph_reference ? graph_reference->clone() : ASTPtr{});
    return result;
}

void PlanScope::adoptBindingsAndKeepGraph(PlanScope source_scope)
{
    auto previous_graph = std::move(active_graph);
    *this = std::move(source_scope);
    active_graph = std::move(previous_graph);
}

}
