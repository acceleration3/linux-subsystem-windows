cmake_minimum_required(VERSION 3.25)

project(LSW
    VERSION 0.0.1
    DESCRIPTION "Linux Subsystem for Windows"
    HOMEPAGE_URL "https://github.com/anthropics/lsw"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static libs may end up linked into shared libs (the ICD .so).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(LSWCompileOptions)

# ---- Options ----
option(LSW_BUILD_SERVER  "Build the host render server (Linux only)" ON)
option(LSW_BUILD_CLIENT  "Build the client Vulkan ICD"               ON)
option(LSW_BUILD_MANAGER "Build the VM manager UI (Linux only)"      ON)
option(LSW_BUILD_AGENT   "Build the Windows guest agent"             ${WIN32})
option(LSW_BUILD_TESTS   "Build unit tests"                          ON)

# ---- Platform sanity ----
if(NOT (UNIX OR WIN32))
    message(WARNING "LSW only targets Linux (host) and Windows (guest); other platforms unsupported")
endif()

if(LSW_BUILD_SERVER AND NOT (UNIX AND NOT APPLE))
    message(STATUS "LSW_BUILD_SERVER=OFF (only supported on Linux)")
    set(LSW_BUILD_SERVER OFF)
endif()

if(LSW_BUILD_MANAGER AND NOT (UNIX AND NOT APPLE))
    message(STATUS "LSW_BUILD_MANAGER=OFF (only supported on Linux)")
    set(LSW_BUILD_MANAGER OFF)
endif()

if(LSW_BUILD_AGENT AND NOT WIN32)
    message(STATUS "LSW_BUILD_AGENT=OFF (only supported on Windows)")
    set(LSW_BUILD_AGENT OFF)
endif()

if(LSW_BUILD_TESTS)
    enable_testing()
endif()

# ---- Dependencies ----
include(LSWDependencies)

# ---- Subprojects ----
# linkproto is the one module BOTH sides compile: the manager and the
# Windows guest agent share it, so it is header-only and depends on
# nothing but the standard library. Added unconditionally — an
# agent-only cross-build needs it exactly as much as a host build does.
add_subdirectory(src/linkproto)

# base / protocol / vkproto: logging, framing and transports, Vulkan
# dispatch. These used to be described as "Linux-side" and skipped for
# the cross build. That stopped being true with the Windows ICD: the
# guest runs the same protocol library, the same generated dispatch and
# the same shared-memory channel as the host. The POSIX-only sources
# inside src/protocol are selected there, not here.
if(LSW_BUILD_SERVER OR LSW_BUILD_CLIENT OR LSW_BUILD_MANAGER OR LSW_BUILD_TESTS)
    add_subdirectory(src/base)
    add_subdirectory(src/protocol)
    add_subdirectory(src/vkproto)
endif()

if(LSW_BUILD_SERVER)
    add_subdirectory(src/render_server)
endif()

if(LSW_BUILD_CLIENT)
    add_subdirectory(src/icd)
endif()

if(LSW_BUILD_MANAGER)
    # Dependencies shared by the host-side modules under src/host.
    # Resolved once here, in the parent scope, so every subdirectory sees
    # the same imported targets and the pkg-config IMPORTED targets are
    # not created twice.
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Concurrent Xml)
    find_package(PkgConfig REQUIRED)
    find_package(ZLIB REQUIRED)  # MicrosoftClient: defensive gunzip fallback
    pkg_check_modules(LIBVIRT REQUIRED IMPORTED_TARGET libvirt)
    pkg_check_modules(LIBISOFS REQUIRED IMPORTED_TARGET libisofs-1)  # PayloadIsoBuilder
    pkg_check_modules(LIBVNCCLIENT REQUIRED IMPORTED_TARGET libvncclient)  # VncView
    # xcb: per-window WM_CLASS on each mirror (X11 taskbar ungrouping).
    pkg_check_modules(XCB IMPORTED_TARGET xcb)
    # libwim (wimlib) — ships no pkg-config / CMake config; resolve by hand.
    find_library(WIMLIB_LIBRARY NAMES wim REQUIRED)
    find_path(WIMLIB_INCLUDE_DIR NAMES wimlib.h REQUIRED)

    add_subdirectory(src/host)
endif()

if(LSW_BUILD_AGENT)
    add_subdirectory(src/agent)
endif()

if(LSW_BUILD_TESTS)
    add_subdirectory(src/protocol/tests)
    # The link contract: the highest-risk code in the repo (both sides of
    # a VM boundary must agree byte for byte) and, now that it lives in
    # one toolchain-neutral module, testable host-native.
    add_subdirectory(src/linkproto/tests)
    # The agent's platform-free logic, compiled by the HOST toolchain —
    # the agent executable itself is Windows-only, but this is not.
    if(NOT WIN32)
        add_subdirectory(src/agent/tests)
    endif()
    # vkproto tests are wired from src/vkproto/CMakeLists.txt
endif()

# ---- Codegen targets ----
add_subdirectory(codegen)

# ---- Summary ----
message(STATUS "")
message(STATUS "LSW configuration:")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Server:           ${LSW_BUILD_SERVER}")
message(STATUS "  Client:           ${LSW_BUILD_CLIENT}")
message(STATUS "  Manager:          ${LSW_BUILD_MANAGER}")
message(STATUS "  Agent:            ${LSW_BUILD_AGENT}")
message(STATUS "  Tests:            ${LSW_BUILD_TESTS}")
message(STATUS "")
