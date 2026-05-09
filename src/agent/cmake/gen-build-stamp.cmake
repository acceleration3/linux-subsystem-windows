# Run by the `lsw_agent_stamp` custom target on every build. Writes a
# fresh timestamp into ${OUT}; the changing content forces
# system_info.cpp (which #includes it) to recompile each build so
# agent_version always changes and redeploy_agent can detect a swap.
string(TIMESTAMP _ts "%Y-%m-%d %H:%M:%S" UTC)
file(WRITE "${OUT}" "#pragma once\n#define LSW_BUILD_STAMP \"${_ts} UTC\"\n")
