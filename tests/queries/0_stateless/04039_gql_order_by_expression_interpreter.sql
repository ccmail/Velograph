SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
RETURN 1 AS a ORDER BY a + 1 LIMIT 1;
