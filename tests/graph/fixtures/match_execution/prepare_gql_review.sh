#!/usr/bin/env bash

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../../.." && pwd)"
clickhouse_binary="${CLICKHOUSE_BINARY:-${repo_root}/build/programs/clickhouse-gql-review}"
data_path="${GQL_REVIEW_DATA_PATH:-${repo_root}/tmp/gql_review}"
seed_file="${script_dir}/seed.sql"

if [[ ! -x "${clickhouse_binary}" ]]; then
    printf 'Missing ClickHouse executable: %s\n' "${clickhouse_binary}" >&2
    printf 'Build the `clickhouse-gql-review` target or set `CLICKHOUSE_BINARY`.\n' >&2
    exit 1
fi

mkdir -p "${data_path}"
"${clickhouse_binary}" local \
    --path "${data_path}" \
    --multiquery \
    --queries-file "${seed_file}"

printf 'Prepared `gql_review` in %s\n' "${data_path}"
