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

# Static libs may end up linked into shared libs.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(LSWCompileOptions)

# ---- Options ----
option(LSW_BUILD_MANAGER "Build the VM manager UI (Linux only)"   ON)
option(LSW_BUILD_AGENT   "Build the Windows guest agent"          ${WIN32})
option(LSW_BUILD_TESTS   "Build unit tests"                       ON)

# ---- Platform sanity ----
if(NOT (UNIX OR WIN32))
    message(WARNING "LSW only targets Linux (host) and Windows (guest); other platforms unsupported")
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
# linkproto is the shared-memory region layout: the byte-for-byte
# contract between the two ends of the VM boundary. Header-only, depends
# on nothing but the standard library, and added unconditionally because
# every configuration needs it — including a cross-build that compiles
# nothing else here.
add_subdirectory(src/linkproto)
add_subdirectory(src/base)

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
    # The highest-risk code here: both ends of a VM boundary must agree
    # byte for byte, and this is where that is checked.
    add_subdirectory(src/linkproto/tests)
    # The agent's platform-free logic, compiled by the HOST toolchain —
    # the agent executable itself is Windows-only, but this is not.
    if(NOT WIN32)
        add_subdirectory(src/agent/tests)
    endif()
endif()

# ---- Summary ----
message(STATUS "")
message(STATUS "LSW configuration:")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Manager:          ${LSW_BUILD_MANAGER}")
message(STATUS "  Agent:            ${LSW_BUILD_AGENT}")
message(STATUS "  Tests:            ${LSW_BUILD_TESTS}")
message(STATUS "")
