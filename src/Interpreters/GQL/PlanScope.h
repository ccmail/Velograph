#pragma once

#include <Core/NamesAndTypes.h>
#include <Parsers/IAST_fwd.h>
#include <base/types.h>

#include <vector>

namespace DB
{
class Block;
}

namespace DB::GQL
{

enum class BindingKind : UInt8
{
    Source,
    Projection,
};

struct PlanBinding
{
    String name;
    DataTypePtr type;
    BindingKind kind = BindingKind::Source;
    ASTPtr expression;
};

class PlanScope final
{
public:
    void replaceWithHeader(const Block & header, BindingKind kind);
    void addOrReplaceBinding(String name, DataTypePtr type, BindingKind kind, ASTPtr expression = {});
    void setActiveGraph(ASTPtr graph_reference_);
    PlanScope makeChildGraphScope() const;
    PlanScope makeGraphOverrideScope(const ASTPtr & graph_reference) const;
    void adoptBindingsAndKeepGraph(PlanScope source_scope);

    bool hasBinding(const String & name) const;
    const PlanBinding * tryGetBinding(const String & name) const;
    const std::vector<PlanBinding> & getBindings() const { return bindings; }
    const ASTPtr & getActiveGraph() const { return active_graph; }

private:
    std::vector<PlanBinding> bindings;
    ASTPtr active_graph;
};

}
