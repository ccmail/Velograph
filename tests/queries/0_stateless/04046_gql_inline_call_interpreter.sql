SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
CALL { RETURN 1 AS a } RETURN a;
