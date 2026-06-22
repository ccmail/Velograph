#include <Storages/Graph/GraphSchemaRegistry.h>

#include <Common/Exception.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>

namespace DB {

namespace ErrorCodes {
extern const int LOGICAL_ERROR;
extern const int UNKNOWN_ELEMENT;
}  // namespace ErrorCodes

static const auto &idType() { return std::make_shared<DataTypeUInt64>(); }
static const auto &rankType() { return std::make_shared<DataTypeUInt64>(); }
static const auto &typeType() { return std::make_shared<DataTypeString>(); }
static const auto &labelsType() { return std::make_shared<DataTypeArray>(std::make_shared<DataTypeString>()); }
static const auto &degreeType() { return std::make_shared<DataTypeUInt64>(); }

void GraphSchemaRegistry::registerVertexType(const String &name, const NamesAndTypes &columns) {
  if (vertex_types.contains(name)) throw Exception(ErrorCodes::LOGICAL_ERROR, "Graph vertex type '{}' is already registered", name);

  vertex_types.emplace(name, GraphVertexType{name, columns});
}

void GraphSchemaRegistry::registerEdgeType(const String &name, const NamesAndTypes &columns, const String &source_vertex_type,
                                           const String &target_vertex_type) {
  if (edge_types.contains(name)) throw Exception(ErrorCodes::LOGICAL_ERROR, "Graph edge type '{}' is already registered", name);

  if (!vertex_types.contains(source_vertex_type))
    throw Exception(ErrorCodes::LOGICAL_ERROR, "Source vertex type '{}' is not registered", source_vertex_type);

  if (!vertex_types.contains(target_vertex_type))
    throw Exception(ErrorCodes::LOGICAL_ERROR, "Target vertex type '{}' is not registered", target_vertex_type);

  edge_types.emplace(name, GraphEdgeType{name, columns, source_vertex_type, target_vertex_type});
}

const GraphVertexType *GraphSchemaRegistry::findVertexType(const String &name) const {
  auto it = vertex_types.find(name);
  return it != vertex_types.end() ? &it->second : nullptr;
}

const GraphEdgeType *GraphSchemaRegistry::findEdgeType(const String &name) const {
  auto it = edge_types.find(name);
  return it != edge_types.end() ? &it->second : nullptr;
}

NamesAndTypes GraphSchemaRegistry::getVerticesTableColumns() const {
  NamesAndTypes columns;
  columns.emplace_back("__ID__", idType());
  columns.emplace_back("labels", labelsType());

  for (const auto &[_, vertex_type] : vertex_types)
    for (const auto &col : vertex_type.columns) columns.push_back(col);

  return columns;
}

NamesAndTypes GraphSchemaRegistry::getEdgesTableColumns() const {
  NamesAndTypes columns;
  columns.emplace_back("__SRC__", idType());
  columns.emplace_back("__RANK__", rankType());
  columns.emplace_back("__DST__", idType());
  columns.emplace_back("type", typeType());

  for (const auto &[_, edge_type] : edge_types)
    for (const auto &col : edge_type.columns) columns.push_back(col);

  return columns;
}

}  // namespace DB
