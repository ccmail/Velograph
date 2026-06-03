SET allow_experimental_gql_dialect = 1;
SET dialect = 'gql';
FOR x IN [1, 2] WITH ORDINALITY ord RETURN x, ord;
