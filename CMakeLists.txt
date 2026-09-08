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

# Static libs may end up linked into shared libs (the guest ICDs).
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

# The render server, and the guest drivers that talk to it. On Linux
# this is the ``rvk-render-server`` binary the manager spawns; on the
# mingw cross build it is the guest's OpenCL, OpenGL and Vulkan ICDs.
# One switch, because it is one submodule either way.
option(LSW_BUILD_RENDER_SERVER "Build the render server / guest ICDs" ON)

# remotewin, the window half. On Linux this is the ``remotewin`` binary
# the manager spawns for a launched application; on the mingw cross
# build it is ``remotewin-guest.exe``, which runs beside that application
# inside the guest. One switch, because it is one submodule either way.
option(LSW_BUILD_REMOTEWIN "Build the window mirror / its guest half" ON)

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

# The render server is a project of its own: it knows how to move
# OpenCL, OpenGL and Vulkan across a shared-memory transport and nothing
# about virtual machines, so it builds and tests without any of this.
#
# LSW consumes it as two artifacts and links neither. On Linux it is the
# ``rvk-render-server`` executable the manager spawns per VM; on the
# mingw cross build it is the guest drivers — ``librvk_icd.dll``,
# ``opengl32.dll``, ``librvk_opencl.dll`` — that the agent links into an
# application's own directory before starting it.
set(LSW_RENDER_SERVER_DIR "${CMAKE_SOURCE_DIR}/external/render-server"
    CACHE PATH "Location of the render-server checkout")
if(LSW_BUILD_RENDER_SERVER)
    if(NOT EXISTS "${LSW_RENDER_SERVER_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "render-server is missing from ${LSW_RENDER_SERVER_DIR}.\n"
            "It is a submodule: git submodule update --init --recursive")
    endif()
    # No demo applications and no test suite: LSW wants the server and
    # the drivers, and the rest is that project's own business when it
    # is built on its own.
    set(RVK_BUILD_GUEST_TOOLS OFF CACHE BOOL "" FORCE)
    set(RVK_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LSW_RENDER_SERVER_DIR}" render-server)
endif()

# The window half, and a project of its own for the same reason: it
# knows how to move a Windows application's windows, its input and its
# cursors across a boundary, and nothing about virtual machines.
#
# LSW consumes it as two artifacts and links neither. On Linux it is the
# ``remotewin`` executable the manager spawns for each launched
# application, one process per application, owning that application's
# windows on this desktop; on the mingw cross build it is
# ``remotewin-guest.exe``, which the agent starts in the guest session
# and which dials back out to that process.
set(LSW_REMOTEWIN_DIR "${CMAKE_SOURCE_DIR}/external/remotewin"
    CACHE PATH "Location of the remotewin checkout")
if(LSW_BUILD_REMOTEWIN)
    if(NOT EXISTS "${LSW_REMOTEWIN_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "remotewin is missing from ${LSW_REMOTEWIN_DIR}.\n"
            "It is a submodule: git submodule update --init --recursive")
    endif()
    # Its own test suite is its own business when it is built on its own,
    # and it wants a Qt that a cross build has no reason to have.
    set(RWIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LSW_REMOTEWIN_DIR}" remotewin)
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
    # hivex (libguestfs) — edits the offline Windows registry hives
    # inside install.wim. Ships no pkg-config either.
    find_library(HIVEX_LIBRARY NAMES hivex REQUIRED)
    find_path(HIVEX_INCLUDE_DIR NAMES hivex.h REQUIRED)

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
message(STATUS "  Render server:    ${LSW_BUILD_RENDER_SERVER}")
message(STATUS "  Tests:            ${LSW_BUILD_TESTS}")
message(STATUS "")
