---
description: 'Current GraphStorage foundation, execution contract, and known limitations'
sidebar_label: 'Graph storage foundation'
sidebar_position: 84
slug: /development/graph/storage-engine
title: 'Graph Storage Foundation'
doc_type: 'reference'
---

# Graph Storage Foundation

`GraphStorage` is the development storage foundation for LeoGraph. It gives the
graph query engine a concrete ClickHouse-backed source while the first `MATCH`
execution slices are being built. It is intentionally merged before the query
engine is complete so storage and query development can proceed on separate
module branches.

This is not yet a production-ready graph storage engine. The current contract is
good enough for a projected vertex scan and for developing the query-plan
lowering around it. Lookup correctness, schema lifecycle, persistence, and
storage-level filtering still require follow-up work. Query completion does not
remove that storage work; it provides the real access patterns needed to finish
and tune it.

## Ownership Boundary

The storage layer owns:

- the `IGraphStorage` physical read contract;
- the `GraphStorageEngine` implementation;
- internal table creation and programmatic writes;
- physical direction handling and projected column headers.

The graph query layer owns:

- graph-name and variable resolution;
- lowering `MATCH` patterns into `scan`, `getVertex`, `getEdge`, and
  `getNeighbors` calls;
- composing multi-step and variable-length traversals;
- logical predicates, projections, and plan optimization.

`MatchStep` contains only the compatibility change required by the new storage
primitive contract. Complete `MATCH` lowering remains graph-query work and is
developed outside the storage branch.

## Physical Layout

Creating `_graph` with `ENGINE = GraphStorage` constructs one
`GraphStorageEngine` for the containing database and creates four internal
tables:

| Internal object | Engine and key | Current purpose |
|-----------------|----------------|-----------------|
| `vertices` | `MergeTree ORDER BY __ID__` | Vertex identifiers, labels, and eventually vertex properties. |
| `edges_forward` | `ReplacingMergeTree(__RANK__) ORDER BY (__SRC__, type, __RANK__)` | Forward adjacency and edge properties. |
| `edges_reverse` | `ReplacingMergeTree(__RANK__) ORDER BY (__DST__, type, __RANK__)` | Reverse adjacency for incoming traversal. |
| `vertex_degrees` | Materialized view with inner `SummingMergeTree ORDER BY (__SRC__, type)` | Forward out-degree counts derived from `edges_forward`. |

`writeEdge` writes both adjacency copies and swaps `__SRC__` / `__DST__` for
the reverse copy. This deliberately trades approximately 2x edge storage for
directional access. The two writes are not atomic, so interrupted writes can
leave the copies inconsistent until a future reconciliation mechanism exists.

The factory registration is a development DDL entry point, not the final graph
catalog interface. `CREATE GRAPH`, graph-type DDL, and schema restoration are
not connected to it yet. `dropInternalTables` can remove the internal objects in
dependency order, but no graph-catalog DDL currently orchestrates that helper;
callers must not assume that dropping `_graph` completes graph cleanup.

## Read Contract

`IGraphStorage` exposes four primitives:

| Primitive | Intended semantics | Current `GraphStorageEngine` behavior |
|-----------|--------------------|---------------------------------------|
| `scan` | Scan vertices or edges with a requested projection. | Implemented as a direct `MergeTreeDataSelectExecutor::read` full scan with column projection. |
| `getVertex` | Fetch only the requested vertex identifiers. | Reads the projected vertex columns, but currently ignores the identifier input and returns a full scan. |
| `getEdge` | Fetch only the requested edge keys. | Reads the projected forward-edge columns, but currently ignores the key input and returns a full scan. |
| `getNeighbors` | Expand only the requested source vertices with direction, predicate, and per-vertex limit. | Chooses the forward, reverse, or both edge tables, but currently ignores input vertices, predicate, and limit. |

Each primitive has three layers: a `NameSet` projection entry point, a resolved
header plus column-mask entry point, and an `xxxImpl` storage override. The base
class validates projections, normalizes direction headers, and supplies a
structure-correct empty source when an implementation returns no pipe.

Only `scan` currently has complete semantics. The other primitives are explicit
full-scan placeholders and must not be treated as correct indexed lookups. This
is why the first query-engine slice is limited to a single vertex pattern such
as `MATCH (n) RETURN n`.

## Write and Schema Status

`writeVertex` and `writeEdge` can feed correctly shaped `Block` objects into the
internal tables. They are programmatic development APIs; `GQL` DML is not wired
to them.

`GraphSchemaRegistry` can register vertex and edge types in memory and can build
the corresponding combined headers. The current lifecycle is incomplete:

- registrations are not persisted or restored after restart;
- registering a type does not alter already-created internal tables;
- cached vertex and edge headers are not invalidated after registration;
- duplicate property names across types have no finalized physical mapping;
- graph catalog DDL does not populate the registry;
- partial internal-table creation is not repaired transactionally.

Consequently, the reliable development slice uses the built-in identity and
label columns. Property-column execution requires the schema lifecycle to be
connected before it can be considered durable.

## What Is Ready

- `GraphStorage` is registered in `StorageFactory`.
- Internal vertex, forward-edge, reverse-edge, and degree objects can be
  created by the factory and removed programmatically by `dropInternalTables`.
- Vertex and edge blocks can be written programmatically.
- Projected vertex and edge scans use the native `MergeTree` read path.
- Direction selects the appropriate adjacency table, with an undirected read
  uniting both copies.
- Projection validation fails closed for unknown columns.
- `supportScan` accurately reports that full scans are available.

This foundation is sufficient to continue the graph query engine toward the
single-node vertical slice. It is not a claim that general traversal is ready.

## Required Follow-up

Correctness and lifecycle work:

1. Connect graph catalog DDL to persistent schema metadata and restore it on
   startup.
2. Make schema changes create or alter physical property columns and invalidate
   cached headers safely.
3. Apply vertex ids, edge keys, and source-id sets in the three lookup
   primitives instead of returning unrelated rows.
4. Implement `filter_pushdown` and `limit_per_vertex` semantics.
5. Define recovery for partial initialization, dual-write inconsistency, and
   graph drop.
6. Add dedicated tests for lifecycle, restart, forward/reverse consistency,
   degree maintenance, all directions, and empty/error paths.

Performance work after the query access patterns stabilize:

1. Build `SelectQueryInfo` filters and `KeyCondition` inputs so the
   `MergeTree` primary index can skip marks for `__ID__`, `__SRC__`, and
   `__DST__` lookups.
2. Add prewhere and predicate pushdown without constructing SQL text in the
   storage hot path.
3. Tune stream counts, block sizes, and resource ownership from query settings.
4. Add statistics and measurements for traversal ordering, edge duplication,
   and degree-assisted planning.
5. Benchmark the physical layout and revisit keys, partitions, and deduplication
   semantics using completed query workloads.

The near-term development order is therefore deliberate: keep this physical
contract stable enough to finish graph-query composition, then return to the
storage layer with executable predicates, expand shapes, and representative
benchmarks.
