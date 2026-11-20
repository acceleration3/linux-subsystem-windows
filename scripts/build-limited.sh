#!/usr/bin/env bash
set -euo pipefail

properties=()
[[ -n ${LSW_BUILD_CPU_QUOTA:-} ]] && properties+=(-p "CPUQuota=${LSW_BUILD_CPU_QUOTA}")
[[ -n ${LSW_BUILD_MEMORY_HIGH:-} ]] && properties+=(-p "MemoryHigh=${LSW_BUILD_MEMORY_HIGH}")
export LSW_BUILD_JOBS="${LSW_BUILD_JOBS:-$(nproc)}"
export CMAKE_BUILD_PARALLEL_LEVEL="${LSW_BUILD_JOBS}"
exec systemd-run --user --scope -q "${properties[@]}" nice -n 19 ionice -c3 "$@"
