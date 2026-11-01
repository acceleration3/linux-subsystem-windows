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

# accel-virt, the GPU. Both of its halves are built by the Linux host
# build — the patched QEMU and executor for the host, and the guest's
# avgpu.sys and user-mode drivers with accel-virt's own clang-cl
# toolchains — so the mingw cross build has nothing to do with it. Off
# builds a manager whose VMs have no paravirtual GPU.
option(LSW_BUILD_ACCEL_VIRT "Build the paravirtual GPU (host QEMU + guest drivers)" ON)

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

# ---- Install layout ----
# What ``cmake --install <tree> --component lsw`` lays out under the
# prefix. The three build trees (this host tree, the mingw x86_64 tree
# and the i686 one, scripts/build-agent.sh) install into the same
# prefix, each its own part:
#
#   bin/lsw-manager
#   lib/lsw/remotewin                       the mirror's host half
#   lib/lsw/accel-virt/qemu/                QEMU, its modules and data,
#                                           the executor
#   lib/lsw/accel-virt/d3d/                 DXVK and vkd3d-proton
#   share/lsw/guest/                        the agent, remotewin-guest.exe,
#                                           av-drivers/, loaders/x64|x86/,
#                                           virtiofs/ (shared folders)
#   lib/lsw/lsw-usb-access                  root helper for USB access
#   share/polkit-1/actions/org.lsw.usb-access.policy
#   share/applications/lsw-manager.desktop
#
# Literal directories rather than GNUInstallDirs: the manager finds all
# of it relative to its own binary (platform::locate_build_artifact), so
# the shape is fixed — a lib64 or a multiarch libdir would only move the
# files away from where it looks. Relative to the prefix, so a DESTDIR
# staging tree is a working install as it stands.
#
# A component of its own because the cross trees fetch Vulkan-Loader,
# whose own install rules (headers, an import library, CMake config) are
# in the default component and have no business in a package.
set(LSW_INSTALL_COMPONENT lsw)
set(LSW_INSTALL_LIBDIR   "lib/lsw")
set(LSW_INSTALL_GUESTDIR "share/lsw/guest")

# ---- Dependencies ----
include(LSWDependencies)

# ---- Subprojects ----
# linkproto is the wire contract: the byte-for-byte agreement between
# the two ends of the VM boundary. Header-only, depends
# on nothing but the standard library, and added unconditionally because
# every configuration needs it — including a cross-build that compiles
# nothing else here.
add_subdirectory(src/linkproto)
add_subdirectory(src/base)

# The window half, and a project of its own for the same reason: it
# knows how to move a Windows application's windows, its input and its
# cursors across a boundary, and nothing about virtual machines.
#
# Applications use its two executable artifacts. The manager also links its
# link library - the guest link, apart from any window system - for the
# embedded WGC desktop preview. On Linux it is the
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
    # The host half is a Wayland client of its own (libwayland-client and
    # the protocols wayland-protocols has), reads keymaps with xkbcommon,
    # synchronises with DRM syncobjs (libdrm) and places windows through
    # KWin over sd-bus. Checked here, all at once, so a missing one is named
    # with the package that has it rather than as whichever check inside
    # remotewin happens to fail first.
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
    # Its own test suite is its own business when it is built on its own.
    # Its probe is out of every default build (EXCLUDE_FROM_ALL there):
    # built by name only, never installed.
    set(RWIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LSW_REMOTEWIN_DIR}" remotewin)
    # The host half is a private executable the manager spawns, so it
    # goes beside the manager's other private files, not on PATH; the
    # guest half is a file the payload ISO carries.
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

# The GPU, and a project of its own as well: a WDDM paravirtual adapter
# for a Windows 11 guest on QEMU/KVM, which knows how to carry Direct3D,
# Vulkan, OpenGL and OpenCL across a virtio-gpu device and nothing about
# provisioning Windows or mirroring its windows.
#
# LSW consumes it as artifacts and links none of them. On the host it is
# a patched QEMU with the virtio-gpu-av-pci device and libav_executor.so,
# which the VM runs on, and DXVK and vkd3d-proton for the host's
# Direct3D; for the guest it is the av-drivers tree —
# avgpu.sys, the user-mode drivers, provision.ps1 — which the payload
# ISO carries and the guest installs as C:\av.
#
# Not add_subdirectory: accel-virt builds only with clang, and brings
# its own Windows toolchains. cmake/LSWAccelVirt.cmake drives it as
# external projects instead, and says where everything lands.
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
    # Dependencies shared by the host-side modules under src/host.
    # Resolved once here, in the parent scope, so every subdirectory sees
    # the same imported targets and the pkg-config IMPORTED targets are
    # not created twice.
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Concurrent Xml)
    find_package(PkgConfig REQUIRED)
    find_package(ZLIB REQUIRED)  # MicrosoftClient: defensive gunzip fallback
    pkg_check_modules(LIBVIRT REQUIRED IMPORTED_TARGET libvirt)
    # virDomainQemuMonitorCommand: QMP passed through libvirt, which is
    # how a resolution change reaches the running virtio-gpu-av device
    # (its ``modes`` property) without a restart.
    pkg_check_modules(LIBVIRT_QEMU REQUIRED IMPORTED_TARGET libvirt-qemu)
    # Host GPU enumeration: each virtio-gpu-av-pci device binds one host
    # GPU by its Vulkan device UUID, so that is what the manager lists.
    find_package(Vulkan REQUIRED)
    pkg_check_modules(LIBISOFS REQUIRED IMPORTED_TARGET libisofs-1)  # PayloadIsoBuilder
    pkg_check_modules(LIBVNCCLIENT REQUIRED IMPORTED_TARGET libvncclient)  # VncView
    # The monitors' modes, for the refresh rates the guest can be given
    # (platform::HostMonitorRates); the mirror's own libdrm check is above.
    pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm>=2.4.116)
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

    # The guest's half of the shared folders: virtio-win's viofs driver
    # and virtiofs.exe, WinFsp, and the script that installs and mounts
    # them, as share/lsw/guest/virtiofs.
    include(LSWGuestShares)

    # The desktop entry and its icon. accel-virt needs nothing of the host
    # system beyond KVM and a GPU driver: every byte the guest shares with
    # the host GPU is that GPU's own memory (docs/host-memory.md).
    install(FILES packaging/lsw-manager.desktop
            DESTINATION share/applications
            COMPONENT ${LSW_INSTALL_COMPONENT})
    install(FILES packaging/lsw-manager.svg
            DESTINATION share/icons/hicolor/scalable/apps
            COMPONENT ${LSW_INSTALL_COMPONENT})
    # The root helper that lets the user open one kind of USB device, and
    # the polkit action it is run under (platform::UsbAccess). The action
    # names /usr/lib/lsw/lsw-usb-access, which is where a package puts
    # this.
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
if(LSW_BUILD_MANAGER AND LSW_BUILD_ACCEL_VIRT)
    message(STATUS "  accel-virt:       ON (${LSW_ACCEL_VIRT_DIR})")
else()
    message(STATUS "  accel-virt:       OFF")
endif()
message(STATUS "  Tests:            ${LSW_BUILD_TESTS}")
message(STATUS "")
