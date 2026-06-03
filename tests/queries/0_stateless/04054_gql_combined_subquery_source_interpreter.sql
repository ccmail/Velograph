SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
SELECT v FROM { RETURN 1 AS v UNION ALL RETURN 2 AS v } ORDER BY v DESC LIMIT 1;
