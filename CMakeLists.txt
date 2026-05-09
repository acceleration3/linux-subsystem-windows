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
# common / protocol / vkproto are Linux-side libraries (logging,
# vsock transport, Vulkan dispatch). The Windows guest agent doesn't
# consume any of them, so when only LSW_BUILD_AGENT is on we skip
# them — they wouldn't compile under mingw anyway (sys/socket.h etc.)
# and they pull in spdlog, which has no mingw package.
if(LSW_BUILD_SERVER OR LSW_BUILD_CLIENT OR LSW_BUILD_MANAGER OR LSW_BUILD_TESTS)
    add_subdirectory(src/common)
    add_subdirectory(src/protocol)
    add_subdirectory(src/vkproto)
endif()

if(LSW_BUILD_SERVER)
    add_subdirectory(src/server)
endif()

if(LSW_BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

if(LSW_BUILD_MANAGER)
    add_subdirectory(src/manager)
endif()

if(LSW_BUILD_AGENT)
    add_subdirectory(src/agent)
endif()

if(LSW_BUILD_TESTS)
    add_subdirectory(src/protocol/tests)
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
