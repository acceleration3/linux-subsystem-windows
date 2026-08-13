# Third-party dependency resolution for LSW.
#
# Three libraries:
#   * spdlog         — system package (Linux only, used by lsw_common).
#   * nlohmann_json  — fetched via FetchContent so the build is hermetic
#                      and identical across native + mingw cross-targets.
#   * Vulkan-Headers — likewise, and for a sharper reason: the ICD has to
#                      cross-compile, and the mingw toolchain cannot see
#                      the host's /usr/include/vulkan.
#
# All expose canonical CMake targets (``spdlog::spdlog``,
# ``nlohmann_json::nlohmann_json``, ``Vulkan::Headers``) so consumers
# don't care where the library actually came from.
#
# Distro packages providing these:
#   Arch:   spdlog, nlohmann-json
#   Debian: libspdlog-dev, nlohmann-json3-dev
#
# vk.xml is separate: it is consumed only by the codegen targets under
# codegen/, which take it from the host's vulkan-headers package.

# ---- spdlog ----
# Required by ``LSW::Base``, which the ICD uses — and the ICD now
# cross-compiles, so "spdlog is Linux-only" stopped being true.
#
# The system package first, because that is what a Linux developer
# already has and what distro packaging wants. Failing that, fetch it:
# there is no mingw spdlog package on most distros, and the alternative
# was giving ``lsw/base/log.hpp`` a second, quietly different
# implementation for the cross build. The agent already has its own
# logger for its own reasons; a third would be the point at which nobody
# can say what a given LSW_WARN does.
#
# Header-only on the fetched path, so the cross build has no library to
# link and no ABI to match.
if(LSW_BUILD_SERVER OR LSW_BUILD_CLIENT OR LSW_BUILD_MANAGER OR LSW_BUILD_TESTS
   OR LSW_BUILD_AGENT)
    find_package(spdlog QUIET CONFIG)
    if(NOT spdlog_FOUND)
        include(FetchContent)
        FetchContent_Declare(spdlog
            URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
            URL_HASH SHA256=9962648c9b4f1a7bbc76fd8d9172555bad1871fdb14ff4f842ef87949682caa5
        )
        set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
        set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
        set(SPDLOG_INSTALL       OFF CACHE INTERNAL "")
        # Header-only: no compiled library, so nothing to cross-link.
        set(SPDLOG_BUILD_SHARED  OFF CACHE INTERNAL "")
        FetchContent_MakeAvailable(spdlog)
        message(STATUS "spdlog: fetched (no system package found)")
    endif()
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

# ---- Vulkan-Headers ----
# Header-only, same treatment and the same reason as nlohmann_json.
#
# ``src/vkproto`` used to put ``third_party/Vulkan-Headers/include`` on
# its include path. That directory has never existed in this repo, so
# the headers were silently coming from /usr/include all along — which
# works exactly until the first cross-build, where the mingw toolchain
# cannot see them and the ICD stops compiling for the target it is for.
#
# Pinned rather than floating: the wire format is generated from a
# specific vk.xml, and the structs the generated serializers memcpy are
# these headers' structs. Those two agreeing is not optional.
set(LSW_VULKAN_HEADERS_TAG "v1.4.309" CACHE STRING
    "Vulkan-Headers tag to fetch")
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        ${LSW_VULKAN_HEADERS_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(VulkanHeaders)

# ---- Vulkan-Loader (guest only) ----
# A VM with no GPU driver has no vulkan-1.dll, and without one nothing
# can find our ICD — the loader is the thing that reads
# HKLM\SOFTWARE\Khronos\Vulkan\Drivers and loads what it names. So the
# guest needs a loader shipped alongside the driver.
#
# Built from source rather than lifted out of a Vulkan SDK: the SDK is
# not redistributable-by-download in a form a script can rely on, and
# building it pins the loader to the same version as the headers the
# wire format was generated from.
#
# Cross-build only. On Linux the system loader is already there and is
# the one that must be used.
if(WIN32 AND LSW_BUILD_CLIENT)
    set(LSW_VULKAN_LOADER_TAG "${LSW_VULKAN_HEADERS_TAG}" CACHE STRING
        "Vulkan-Loader tag to fetch (keep in step with the headers)")
    FetchContent_Declare(VulkanLoader
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader.git
        GIT_TAG        ${LSW_VULKAN_LOADER_TAG}
        GIT_SHALLOW    TRUE
    )
    set(BUILD_TESTS  OFF CACHE INTERNAL "")
    set(UPDATE_DEPS  OFF CACHE INTERNAL "")
    set(BUILD_WSI_XCB_SUPPORT     OFF CACHE INTERNAL "")
    set(BUILD_WSI_XLIB_SUPPORT    OFF CACHE INTERNAL "")
    set(BUILD_WSI_WAYLAND_SUPPORT OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(VulkanLoader)
endif()
