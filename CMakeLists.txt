cmake_minimum_required(VERSION 3.25)

project(LSW
    VERSION 0.0.1
    DESCRIPTION "Linux Subsystem for Windows"
    HOMEPAGE_URL "https://github.com/anthropics/lsw"
    LANGUAGES CXX
)

# remotevk carries the cmake modules that define LSW::CompileOptions and
# resolve the shared third-party dependencies, so its module directory
# has to be on the path before anything here includes them. LSW's own
# cmake/ still holds the mingw toolchain file, which is selected on the
# command line rather than included.
set(LSW_REMOTEVK_DIR "${CMAKE_SOURCE_DIR}/external/remotevk"
    CACHE PATH "Location of the remotevk checkout")
if(NOT EXISTS "${LSW_REMOTEVK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "remotevk is missing from ${LSW_REMOTEVK_DIR}.\n"
        "It is a submodule: git submodule update --init --recursive")
endif()
list(APPEND CMAKE_MODULE_PATH "${LSW_REMOTEVK_DIR}/cmake" "${CMAKE_SOURCE_DIR}/cmake")

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

# LSW::CompileOptions and the shared third-party dependencies are
# defined by remotevk when it is added below; including those modules
# here as well would define the same targets twice. Everything that
# uses them (src/host, src/agent) is added after remotevk.

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


# ---- Subprojects ----
# The Vulkan proxy — the ICD, the render server, the protocol and the
# shared-memory region layout — lives in remotevk and is consumed from
# here. It is independent of everything else in this repository: it
# knows how to move Vulkan across a transport and nothing about virtual
# machines, so it can be built and tested without any of this.
#
# The manager and the guest agent both link its LSW::LinkProto and
# LSW::Protocol targets. That is the whole of the coupling: the region
# layout and the framing are a contract the two ends of the VM boundary
# must agree on, and there is exactly one definition of it.
set(REMOTEVK_BUILD_SERVER ${LSW_BUILD_SERVER} CACHE BOOL "" FORCE)
set(REMOTEVK_BUILD_CLIENT ${LSW_BUILD_CLIENT} CACHE BOOL "" FORCE)
set(REMOTEVK_BUILD_TESTS  ${LSW_BUILD_TESTS}  CACHE BOOL "" FORCE)
add_subdirectory("${LSW_REMOTEVK_DIR}" remotevk)

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
    # The agent's platform-free logic, compiled by the HOST toolchain —
    # the agent executable itself is Windows-only, but this is not.
    #
    # The protocol, link-contract and codegen tests moved to remotevk
    # with the code they cover, and run from there.
    if(NOT WIN32)
        add_subdirectory(src/agent/tests)
    endif()
endif()

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
