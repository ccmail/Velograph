SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
FOR x IN [1, 1, 2] RETURN x, COUNT(*) AS c GROUP BY x ORDER BY x;
