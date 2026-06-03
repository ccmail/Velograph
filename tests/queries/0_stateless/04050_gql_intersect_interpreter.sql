SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
FOR x IN [1, 2] RETURN x INTERSECT FOR x IN [2, 3] RETURN x;
