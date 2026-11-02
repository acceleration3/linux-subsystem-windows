#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
DB="${BUILD}/compile_commands.json"

if [[ ! -f "${DB}" ]]; then
    echo "no ${DB}; configure the host build first" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
sed 's/-mno-direct-extern-access //g' "${DB}" > "${WORK}/compile_commands.json"

mapfile -t FILES < <(SELECT_ROOT="${ROOT}" python3 "${ROOT}/scripts/tidy_files.py" \
    "${WORK}/compile_commands.json" "$@")
if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "nothing in ${DB} matches ${*:-src}" >&2
    exit 1
fi

clang-tidy -p "${WORK}" --quiet "${FILES[@]}"
