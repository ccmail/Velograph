SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
SELECT a FROM { RETURN 1 AS a } WHERE a = 1 ORDER BY a LIMIT 1;
