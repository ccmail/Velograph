# LDBC GQL Test Suite

This directory contains GQL versions of LDBC (Linked Data Benchmark Council) Social Network
Benchmark queries, converted from the official Cypher reference implementations.

## Purpose

These queries serve as **test-driven development (TDD) targets** for the Velograph GQL engine.
Each query is annotated with its current support status and the GQL features it requires.
As the engine evolves, more queries will transition from PARSE_FAIL/EXEC_FAIL to PARSE_OK/EXEC_OK.

## Source

- **Original Cypher**: https://github.com/ldbc/ldbc_snb_interactive_v1_impls/tree/main/cypher/queries
- **GQL Grammar**: https://github.com/opengql/grammar (ISO/IEC 39075)
- **LDBC SNB Spec**: https://ldbcouncil.org/benchmarks/snb/

## Directory Structure

```
tests/graph/ldbc/
  README.md                  -- This file
  snb-interactive/
    short-queries.gql        -- IS1-IS7: short interactive queries
    complex-queries.gql      -- IC1-IC14: complex interactive queries
    opengql-samples.gql      -- Samples from opengql/grammar repository
```

## Status Annotations

Each query is annotated with:

```
-- STATUS: PARSE_OK | PARSE_FAIL | EXEC_OK | EXEC_FAIL
-- FEATURES: comma-separated list of required GQL features
-- ORIGIN: source query identifier
```

Status meanings:
- `PARSE_OK`: Parser successfully builds AST
- `PARSE_FAIL`: Parser cannot handle this syntax (feature not yet implemented)
- `EXEC_OK`: Full end-to-end execution works
- `EXEC_FAIL`: Parsing works but execution fails (interpreter/operator not ready)

## Cypher-to-GQL Key Differences

| Cypher | GQL (ISO) |
|--------|-----------|
| `(n:Person {id: $personId})` | `(n:Person WHERE n.id = $personId)` |
| `-[:KNOWS*1..3]-` | `-[:KNOWS]->{1,3}` |
| `shortestPath((a)-[*]-(b))` | `ANY SHORTEST MATCH (a)-[]->(b)` |
| `OPTIONAL MATCH` | `OPTIONAL MATCH` (same) |
| `WHERE NOT a=b` | `WHERE a <> b` |
| `UNWIND list AS x` | `FOR x IN list` |
| `WITH ... AS ...` | (binding table / LET) |
| `RETURN ... ORDER BY ... LIMIT` | `RETURN ... ORDER BY ... LIMIT` (same) |
| `collect(x)` | `LIST(x)` |
| `size(list)` | `SIZE(list)` |
| `toInteger(x)` | `CAST(x AS INT)` |
| `coalesce(a, b)` | `COALESCE(a, b)` |
| `reduce(...)` | (procedural / user-defined) |
| `:HAS_TYPE|IS_SUBCLASS_OF*0..` | `-[:HAS_TYPE\|IS_SUBCLASS_OF]->{0,}` |

## LDBC SNB Data Model

The Social Network Benchmark uses the following graph schema:

```
Nodes: Person, City, Country, Continent, University, Company,
       Post, Comment, Message, Forum, Tag, TagClass

Edges: KNOWS, IS_LOCATED_IN, IS_PART_OF, STUDY_AT, WORK_AT,
       HAS_CREATOR, HAS_TAG, HAS_TYPE, IS_SUBCLASS_OF,
       HAS_MEMBER, CONTAINER_OF, REPLY_OF, LIKES, HAS_INTEREST,
       HAS_MODERATOR
```
