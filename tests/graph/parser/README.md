# GQL Parser Tests

## C++ Unit Tests (gtest)

The C++ unit tests for the GQL parser are located at:

```
src/Parsers/graph/tests/gtest_gql_parser.cpp
```

This follows ClickHouse convention where gtest files under `src/` are auto-discovered
and compiled into `unit_tests_dbms`.

### Running the tests

```bash
# Build the test binary
ninja -C build unit_tests_dbms

# Run all GQL parser tests
./build/src/unit_tests_dbms --gtest_filter='GQLParser*'
```

### Test coverage

| Category | Tests |
|----------|-------|
| Node patterns | Simple node scan, node without label |
| Edge patterns | Right/Left edges, multi-hop paths |
| Label expressions | Conjunction, Disjunction, Negation, Wildcard |
| Path quantifiers | `*`, `+`, `?`, `{n}`, `{n,m}` |
| WHERE clause | Comparison, equality, boolean AND, property access |
| RETURN clause | Basic return, DISTINCT |
| AST formatting | Node, edge, quantifier format roundtrip |
| Error handling | Invalid syntax |
| Clone | Node clone, edge with quantifier clone |

## Functional Test Cases

The files below contain GQL queries and expected parse results for manual testing:

- `test_cases.gql` - GQL query test cases with expected behavior annotations
