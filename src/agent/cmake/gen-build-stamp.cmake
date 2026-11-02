string(TIMESTAMP _ts "%Y-%m-%d %H:%M:%S" UTC)
file(WRITE "${OUT}" "#pragma once\n#define LSW_BUILD_STAMP \"${_ts} UTC\"\n")
