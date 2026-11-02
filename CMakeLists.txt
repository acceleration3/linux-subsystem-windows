cmake_minimum_required(VERSION 3.25)

project(LSW
    VERSION 0.2.0
    DESCRIPTION "Linux Subsystem for Windows"
    HOMEPAGE_URL "https://github.com/anthropics/lsw"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(LSWCompileOptions)

option(LSW_BUILD_MANAGER "Build the VM manager UI (Linux only)"   ON)
option(LSW_BUILD_AGENT   "Build the Windows guest agent"          ${WIN32})
option(LSW_BUILD_TESTS   "Build unit tests"                       ON)

option(LSW_BUILD_ACCEL_VIRT "Build the paravirtual GPU (host QEMU + guest drivers)" ON)

option(LSW_BUILD_REMOTEWIN "Build the window mirror / its guest half" ON)

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

set(LSW_INSTALL_COMPONENT lsw)
set(LSW_INSTALL_LIBDIR   "lib/lsw")
set(LSW_INSTALL_GUESTDIR "share/lsw/guest")

include(LSWDependencies)

add_subdirectory(src/linkproto)
if(NOT WIN32)
    add_subdirectory(src/base)
endif()

set(LSW_REMOTEWIN_DIR "${CMAKE_SOURCE_DIR}/external/remotewin"
    CACHE PATH "Location of the remotewin checkout")
if(LSW_BUILD_REMOTEWIN)
    if(NOT EXISTS "${LSW_REMOTEWIN_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "remotewin is missing from ${LSW_REMOTEWIN_DIR}.\n"
            "It is a submodule: git submodule update --init --recursive")
    endif()
    if(NOT WIN32)
        include(CheckLanguage)
        check_language(C)
        find_package(PkgConfig QUIET)
        set(_lsw_rwin_missing "")
        if(NOT CMAKE_C_COMPILER)
            list(APPEND _lsw_rwin_missing "a C compiler (the generated Wayland protocol code)")
        endif()
        if(NOT PkgConfig_FOUND)
            list(APPEND _lsw_rwin_missing "pkg-config (pkgconf)")
        else()
            foreach(_lsw_pc wayland-client wayland-scanner "wayland-protocols>=1.38" xkbcommon "libdrm>=2.4.116"
                            libsystemd)
                string(MAKE_C_IDENTIFIER "${_lsw_pc}" _lsw_pc_var)
                pkg_check_modules(_lsw_rwin_${_lsw_pc_var} QUIET "${_lsw_pc}")
                if(NOT _lsw_rwin_${_lsw_pc_var}_FOUND)
                    list(APPEND _lsw_rwin_missing "${_lsw_pc} (pkg-config)")
                endif()
            endforeach()
        endif()
        if(_lsw_rwin_missing)
            list(JOIN _lsw_rwin_missing "\n  " _lsw_rwin_missing)
            message(FATAL_ERROR
                "remotewin's host half needs, and this system lacks:\n  ${_lsw_rwin_missing}\n"
                "On Arch: pacman -S --needed gcc pkgconf wayland wayland-protocols libxkbcommon libdrm "
                "systemd-libs\n"
                "-DLSW_BUILD_REMOTEWIN=OFF builds the manager without the window mirror.")
        endif()
        enable_language(C)
    endif()
    set(RWIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LSW_REMOTEWIN_DIR}" remotewin)
    if(TARGET remotewin)
        install(TARGETS remotewin
                RUNTIME DESTINATION ${LSW_INSTALL_LIBDIR}
                COMPONENT ${LSW_INSTALL_COMPONENT})
    endif()
    if(TARGET remotewin-guest)
        install(TARGETS remotewin-guest
                RUNTIME DESTINATION ${LSW_INSTALL_GUESTDIR}
                COMPONENT ${LSW_INSTALL_COMPONENT})
    endif()
endif()

set(LSW_ACCEL_VIRT_DIR "${CMAKE_SOURCE_DIR}/external/accel-virt"
    CACHE PATH "Location of the accel-virt checkout")
if(LSW_BUILD_MANAGER AND LSW_BUILD_ACCEL_VIRT)
    if(NOT EXISTS "${LSW_ACCEL_VIRT_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "accel-virt is missing from ${LSW_ACCEL_VIRT_DIR}.\n"
            "It is a submodule: git submodule update --init --recursive")
    endif()
    include(LSWAccelVirt)
endif()

if(LSW_BUILD_MANAGER)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Concurrent Xml)
    find_package(PkgConfig REQUIRED)
    find_package(ZLIB REQUIRED)
    pkg_check_modules(LIBVIRT REQUIRED IMPORTED_TARGET libvirt)
    pkg_check_modules(LIBVIRT_QEMU REQUIRED IMPORTED_TARGET libvirt-qemu)
    find_package(Vulkan REQUIRED)
    pkg_check_modules(LIBISOFS REQUIRED IMPORTED_TARGET libisofs-1)
    pkg_check_modules(LIBVNCCLIENT REQUIRED IMPORTED_TARGET libvncclient)
    pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm>=2.4.116)
    pkg_check_modules(XCB IMPORTED_TARGET xcb)
    find_library(WIMLIB_LIBRARY NAMES wim REQUIRED)
    find_path(WIMLIB_INCLUDE_DIR NAMES wimlib.h REQUIRED)
    find_library(HIVEX_LIBRARY NAMES hivex REQUIRED)
    find_path(HIVEX_INCLUDE_DIR NAMES hivex.h REQUIRED)

    add_subdirectory(src/host)

    include(LSWGuestShares)

    include(LSWGuestUpdate)

    install(FILES packaging/lsw-manager.desktop
            DESTINATION share/applications
            COMPONENT ${LSW_INSTALL_COMPONENT})
    install(FILES packaging/lsw-manager.svg
            DESTINATION share/icons/hicolor/scalable/apps
            COMPONENT ${LSW_INSTALL_COMPONENT})
    install(PROGRAMS packaging/lsw-usb-access
            DESTINATION ${LSW_INSTALL_LIBDIR}
            COMPONENT ${LSW_INSTALL_COMPONENT})
    install(FILES packaging/org.lsw.usb-access.policy
            DESTINATION share/polkit-1/actions
            COMPONENT ${LSW_INSTALL_COMPONENT})
endif()

if(LSW_BUILD_AGENT)
    add_subdirectory(src/agent)
endif()

if(LSW_BUILD_TESTS)
    add_subdirectory(src/linkproto/tests)
    if(NOT WIN32)
        add_subdirectory(src/agent/tests)
    endif()
endif()

message(STATUS "")
message(STATUS "LSW configuration:")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Manager:          ${LSW_BUILD_MANAGER}")
message(STATUS "  Agent:            ${LSW_BUILD_AGENT}")
if(LSW_BUILD_MANAGER AND LSW_BUILD_ACCEL_VIRT)
    message(STATUS "  accel-virt:       ON (${LSW_ACCEL_VIRT_DIR})")
else()
    message(STATUS "  accel-virt:       OFF")
endif()
message(STATUS "  Tests:            ${LSW_BUILD_TESTS}")
message(STATUS "")
