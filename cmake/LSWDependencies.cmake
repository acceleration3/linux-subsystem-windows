# Third-party dependency resolution for LSW.
#
# Two libraries:
#   * spdlog        — system package (Linux only, used by lsw_common).
#   * nlohmann_json — fetched via FetchContent so the build is hermetic
#                     and identical across native + mingw cross-targets.
#
# Both expose canonical CMake targets (``spdlog::spdlog`` and
# ``nlohmann_json::nlohmann_json``) so consumers don't care where the
# library actually came from.
#
# Distro packages providing these:
#   Arch:   spdlog, nlohmann-json
#   Debian: libspdlog-dev, nlohmann-json3-dev
#
# vk.xml (Vulkan-Headers) is not a build-time dependency — it's only
# consumed by the codegen targets, which live under codegen/.

# ---- spdlog ----
# Required by lsw_common's logging on Linux (server, client, manager,
# tests). The Windows guest agent has its own log module and does not
# link spdlog, so when only the agent is being cross-built (mingw,
# no Linux targets) the find is skipped — there's no mingw package
# on most distros.
if(LSW_BUILD_SERVER OR LSW_BUILD_CLIENT OR LSW_BUILD_MANAGER OR LSW_BUILD_TESTS)
    find_package(spdlog REQUIRED CONFIG)
endif()

# ---- nlohmann/json ----
# Header-only. Pulled at configure time via FetchContent so the build
# is hermetic across native + mingw cross-targets — the upstream
# tarball ships a tiny CMakeLists that defines the canonical
# ``nlohmann_json::nlohmann_json`` INTERFACE target. Cached under
# ``${CMAKE_BINARY_DIR}/_deps`` so subsequent configures don't
# re-download.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
