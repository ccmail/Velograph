#pragma once

#include <Core/NamesAndTypes.h>
#include <DataTypes/IDataType.h>

#include <unordered_map>
#include <vector>

namespace DB {

/** Strong-schema definition for a single vertex type within a graph.
 *
 * Example: VERTEX Person (name String, age UInt8)
 */
struct GraphVertexType {
  String name;
  NamesAndTypes columns;  /// user-defined property columns (excludes __ID__)
};

/** Strong-schema definition for a single edge type within a graph.
 *
 * Example: EDGE KNOWS (since Date) FROM Person TO Person
 */
struct GraphEdgeType {
  String name;
  NamesAndTypes columns;  /// user-defined property columns (excludes __SRC__, __RANK__, __DST__, type)
  String source_vertex_type;
  String target_vertex_type;
};

/** Schema registry for a single graph.
 *
 * Maps vertex type names → column definitions, edge type names → column definitions
 * + source/target vertex types. Used by GraphStorageEngine to build MergeTree
 * table schemas and by the GQL planner to resolve labels.
 */
class GraphSchemaRegistry {
 public:
  GraphSchemaRegistry() = default;

  /// Register a new vertex type. Throws if the name already exists.
  void registerVertexType(const String& name, const NamesAndTypes& columns);

  /// Register a new edge type. Throws if the name already exists or if
  /// source/target vertex types are not registered.
  void registerEdgeType(const String& name, const NamesAndTypes& columns, const String& source_vertex_type,
                        const String& target_vertex_type);

  /// Lookup
  const GraphVertexType* findVertexType(const String& name) const;
  const GraphEdgeType* findEdgeType(const String& name) const;

  bool hasVertexType(const String& name) const { return vertex_types.contains(name); }
  bool hasEdgeType(const String& name) const { return edge_types.contains(name); }

  const std::unordered_map<String, GraphVertexType>& getVertexTypes() const { return vertex_types; }
  const std::unordered_map<String, GraphEdgeType>& getEdgeTypes() const { return edge_types; }

  /// Build the full column list for the internal vertices table.
  /// Returns: __ID__, labels (Array(String)), <user columns...>
  NamesAndTypes getVerticesTableColumns() const;

  /// Build the full column list for the internal forward/reverse edge tables.
  /// Returns: __SRC__, __RANK__, __DST__, type, <user columns...>
  NamesAndTypes getEdgesTableColumns() const;

 private:
  std::unordered_map<String, GraphVertexType> vertex_types;
  std::unordered_map<String, GraphEdgeType> edge_types;
};

}  // namespace DB
