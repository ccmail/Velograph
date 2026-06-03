SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
SELECT DISTINCT * FROM { RETURN 1 AS a };
