# Third-party dependency resolution for LSW.
#
# Two libraries, and one of them is only for the guest:
#   * spdlog         — LSW::Base's sink. System package where there is
#                      one, fetched otherwise, because no distro ships a
#                      mingw build of it and the cross-compiled agent
#                      needs the same LSW_WARN as the manager.
#   * nlohmann_json  — every RPC payload, both sides of the boundary.
#   * Vulkan-Loader  — vulkan-1.dll for the guest. Not a build
#                      dependency of anything here; see below.
#
# Distro packages providing these:
#   Arch:   spdlog, nlohmann-json
#   Debian: libspdlog-dev, nlohmann-json3-dev

# ---- spdlog ----
# Header-only on the fetched path, so the cross build has no library to
# link and no ABI to match.
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
    set(SPDLOG_BUILD_SHARED  OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(spdlog)
    message(STATUS "spdlog: fetched (no system package found)")
endif()

# ---- nlohmann/json ----
# Pulled at configure time so the build is hermetic across the native
# and the mingw cross target — the upstream tarball ships a tiny
# CMakeLists defining the canonical INTERFACE target.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ---- Vulkan-Loader (guest only) ----
# Windows ships no vulkan-1.dll, and without one nothing in the guest
# can find a Vulkan driver — the loader is the thing that reads the
# display adapter's VulkanDriverName (VulkanDriverNameWow for a 32-bit
# process) and loads what it names. Applications and DXVK call into it,
# and accel-virt's avvk.dll / avvk32.dll, which its avgpu.inf registers
# there, is what it finds. Both architectures are built: x86-64 here,
# i686 in build-agent32 (scripts/build-agent.sh).
#
# So this is not something LSW links. It is a FILE the wizard stages
# into the guest, and it is built from source because the Vulkan SDK is
# not redistributable-by-download in a form a script can rely on.
#
# Cross-build only. On Linux the system loader is already there.
if(WIN32)
    set(LSW_VULKAN_LOADER_TAG "v1.4.357" CACHE STRING
        "Vulkan-Loader tag to fetch for the guest")
    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        ${LSW_VULKAN_LOADER_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(VulkanHeaders)
    # Vulkan-Loader asks for the headers with find_package(... QUIET) and
    # then compares ``VulkanHeaders_VERSION`` without checking that it
    # found anything. Fetched headers are not an installed package, so
    # that variable is empty and the comparison fails to parse — the
    # configure dies inside the loader's own CMakeLists with "Unknown
    # arguments specified", naming neither this nor the cause. Both come
    # from the same tag; say so.
    string(REGEX REPLACE "^v" "" _lsw_vulkan_version "${LSW_VULKAN_LOADER_TAG}")
    set(VulkanHeaders_VERSION "${_lsw_vulkan_version}")
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
