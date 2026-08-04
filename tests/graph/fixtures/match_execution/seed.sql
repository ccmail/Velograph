-- Reusable graph fixture for `MATCH` execution review and debugger sessions.
-- Re-running this file resets `gql_review` to the same rows.
--
-- Import with:
--   clickhouse local --path tmp/gql_review --multiquery \
--       --queries-file tests/graph/fixtures/match_execution/seed.sql

CREATE DATABASE IF NOT EXISTS gql_review;
USE gql_review;

-- Creating `_graph` initializes the internal graph tables on a fresh data path.
CREATE TABLE IF NOT EXISTS _graph (_ UInt8) ENGINE = GraphStorage;

-- `DROP DATABASE` currently trips a Debug assertion for UUID-less graph tables.
-- Truncating every writable internal table keeps repeated imports deterministic.
TRUNCATE TABLE vertex_degrees;
TRUNCATE TABLE edges_reverse;
TRUNCATE TABLE edges_forward;
TRUNCATE TABLE vertices;

-- Stable vertex IDs intentionally cover single/multiple labels and an isolated vertex.
INSERT INTO vertices (__ID__, labels) VALUES
    (10, ['Person']),
    (20, ['Company']),
    (30, ['Person']),
    (40, ['City']),
    (50, ['Person', 'Employee']),
    (60, ['Company']),
    (70, ['Tag']),
    (80, ['Forum']),
    (90, ['Person']);

-- Forward adjacency contains a `KNOWS` cycle plus several heterogeneous edge types.
INSERT INTO edges_forward (__SRC__, __RANK__, __DST__, type) VALUES
    (10, 1, 30, 'KNOWS'),
    (30, 1, 50, 'KNOWS'),
    (50, 1, 10, 'KNOWS'),
    (10, 1, 20, 'WORK_AT'),
    (50, 1, 60, 'WORK_AT'),
    (10, 1, 40, 'IS_LOCATED_IN'),
    (20, 1, 40, 'IS_LOCATED_IN'),
    (30, 1, 70, 'HAS_INTEREST'),
    (30, 1, 80, 'HAS_MEMBER');

-- `writeEdge` normally creates this swapped copy; the SQL fixture writes it explicitly.
INSERT INTO edges_reverse (__SRC__, __RANK__, __DST__, type) VALUES
    (30, 1, 10, 'KNOWS'),
    (50, 1, 30, 'KNOWS'),
    (10, 1, 50, 'KNOWS'),
    (20, 1, 10, 'WORK_AT'),
    (60, 1, 50, 'WORK_AT'),
    (40, 1, 10, 'IS_LOCATED_IN'),
    (40, 1, 20, 'IS_LOCATED_IN'),
    (70, 1, 30, 'HAS_INTEREST'),
    (80, 1, 30, 'HAS_MEMBER');

-- Leave delayed interactive review sessions ready to parse `GQL`.
SET dialect = 'gql';
