#!/usr/bin/env bash
# Run clang-tidy over the host sources.
#
# Not a plain `clang-tidy -p build`: Qt6 puts -mno-direct-extern-access on
# every target that links it, guarded by $<CXX_COMPILER_ID:GNU>, so a GCC
# build writes that flag into compile_commands.json and clang's driver
# rejects it. The file is still analysed, but every Qt translation unit
# reports a spurious clang-diagnostic-error that buries the real findings.
# So the database is filtered into a copy and clang-tidy pointed at that.
#
# Run: ./scripts/tidy.sh                 # everything
#      ./scripts/tidy.sh src/host/src/vm # one directory or file

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

# The file list comes from the database, not from find. Anything else
# picks up the guest agent, which is Windows-only and is compiled by the
# mingw cross-build rather than this one: clang-tidy would fall back to
# default flags, fail to find windows.h, and report a file-not-found
# error per translation unit that says nothing about the code.
mapfile -t FILES < <(SELECT_ROOT="${ROOT}" python3 "${ROOT}/scripts/tidy_files.py" \
    "${WORK}/compile_commands.json" "$@")
if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "nothing in ${DB} matches ${*:-src}" >&2
    exit 1
fi

clang-tidy -p "${WORK}" --quiet "${FILES[@]}"
