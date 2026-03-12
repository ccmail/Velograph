#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
GRAPH_DIR="${SCRIPT_DIR}/.."
GENERATED_DIR="${GRAPH_DIR}/generated"

rm -rf "${GENERATED_DIR}"
mkdir -p "${GENERATED_DIR}"

ANTLR4_TOOLS_ENV_DIR=$(mktemp -d)
python3 -m venv "${ANTLR4_TOOLS_ENV_DIR}"
"${ANTLR4_TOOLS_ENV_DIR}/bin/pip3" install antlr4-tools

(cd "${SCRIPT_DIR}" && \
    "${ANTLR4_TOOLS_ENV_DIR}/bin/antlr4" \
        -o "${GENERATED_DIR}" \
        -Dlanguage=Cpp \
        -visitor \
        -no-listener \
        GQL.g4 \
        -package gql_grammar)

rm -rf "${ANTLR4_TOOLS_ENV_DIR}"

echo "Generated GQL C++ files in ${GENERATED_DIR}"
ls -la "${GENERATED_DIR}"
