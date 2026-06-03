SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
SELECT n FROM g { MATCH (n) RETURN n };
USE g CALL { MATCH (n) RETURN n } RETURN n;
