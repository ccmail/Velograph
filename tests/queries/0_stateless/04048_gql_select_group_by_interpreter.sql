SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
SELECT x, COUNT(*) AS c FROM { FOR x IN [1, 1, 2] RETURN x } GROUP BY x ORDER BY x;
