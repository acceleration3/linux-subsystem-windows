# accel-virt, the GPU: a WDDM paravirtual adapter for the Windows guest.
#
# Included from the root CMakeLists for the Linux host build only. It
# defines four targets, all part of ``all``:
#
#   accel_virt_fetch    what accel-virt fetches rather than commits: the
#                       Windows kit subset, the Khronos registries, the
#                       QEMU tarball, the OpenCL loader's source, and its
#                       dxvk / vkd3d-proton submodules
#   accel_virt_drivers  the guest half: avgpu.sys and the user-mode
#                       drivers for x64 and x86, installed as the
#                       guest's C:\av, plus the OpenCL loaders
#   accel_virt_host     the host half: the patched QEMU with the
#                       virtio-gpu-av-pci device, libav_executor.so and
#                       av-renderer
#   accel_virt_d3d      the host's Direct3D: DXVK and vkd3d-proton,
#                       built natively, which the executor loads
#
# and ``accel_virt``, which is all of them.
#
# Why ExternalProject and not add_subdirectory: accel-virt refuses any
# compiler but clang (its kernel driver is the same core compiled with
# clang-cl /kernel, and the native tests hold the core to that subset),
# and it brings its own clang-cl toolchains for the guest. Neither can
# live inside this build's gcc configuration, so each half is configured
# as a project of its own, with clang, and LSW consumes only what it
# installs or leaves in its build tree.
#
# What comes out, and where the manager looks for it. The defaults are
# relative to the manager executable (src/host/lsw-manager), so this
# layout is load-bearing; each has an environment override:
#
#   ${CMAKE_BINARY_DIR}/av-drivers/                  LSW_AV_DRIVERS
#       provision.ps1, adapters.txt, package/...     the install tree, which
#       loaders/x64/OpenCL.dll                       the payload ISO carries
#       loaders/x86/OpenCL.dll                       and the guest copies to C:\av
#   ${CMAKE_BINARY_DIR}/accel-virt/qemu/             LSW_AV_QEMU_BUILD
#       qemu/qemu-system-x86_64                      (QEMU_MODULE_DIR is qemu/)
#       libav_executor.so                            the device's executor=
#       av-renderer                                  the device's renderer=
#   ${CMAKE_BINARY_DIR}/accel-virt/d3d/lib/          LSW_AV_D3D_PATH
#       libdxvk_*.so, libvkd3d-proton-*.so           AV_D3D_PATH for QEMU
#
# The guest's vulkan-1.dll loaders (x64 and x86) are not part of this:
# they come from our own mingw cross builds (scripts/build-agent.sh) and
# the payload stager adds them beside the tree.

include(ExternalProject)

find_program(LSW_CLANG   NAMES clang)
find_program(LSW_CLANGXX NAMES clang++)
if(NOT LSW_CLANG OR NOT LSW_CLANGXX)
    message(FATAL_ERROR
        "accel-virt needs clang and clang++ (with clang-cl, lld-link, llvm-lib, "
        "llvm-rc and llvm-mt for its guest toolchains).\n"
        "Install the LLVM toolchain, or configure with -DLSW_BUILD_ACCEL_VIRT=OFF "
        "to build the manager without a GPU.")
endif()
find_program(LSW_MESON NAMES meson)
if(NOT LSW_MESON)
    message(FATAL_ERROR
        "accel-virt builds the host's DXVK and vkd3d-proton with meson, which was not found.\n"
        "Install meson (and glslang), or configure with -DLSW_BUILD_ACCEL_VIRT=OFF.")
endif()
find_package(Git REQUIRED)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# Empty means "the submodule's own third_party/windows-kit, fetched by
# the build". The kit is a few dozen MB, but the WDK and SDK packages it
# is cut from are gigabytes, so a machine that already has one fetched
# (another accel-virt checkout, say) can point here instead.
set(AV_WINDOWS_KIT_DIR "" CACHE PATH
    "An already-fetched accel-virt Windows kit; empty fetches one into the submodule")
if(AV_WINDOWS_KIT_DIR)
    set(_lsw_av_kit "${AV_WINDOWS_KIT_DIR}")
else()
    set(_lsw_av_kit "${LSW_ACCEL_VIRT_DIR}/third_party/windows-kit")
endif()

set(LSW_AV_BUILD_DIR   "${CMAKE_BINARY_DIR}/accel-virt")
set(LSW_AV_DRIVERS_DIR "${CMAKE_BINARY_DIR}/av-drivers")
set(_lsw_av_step       "${CMAKE_CURRENT_LIST_DIR}/LSWAccelVirtStep.cmake")

# ---- prerequisites ----
# Run on every build, and cheap when there is nothing to do: each fetch
# script checks its own pinned marker first and returns at once when
# the tree it would write is already there, so nothing is downloaded
# twice. Asking the script rather than testing for a directory here is
# deliberate — after the submodule is bumped to new pins, the directory
# exists and is wrong, and only the script knows that.
#
# Before either half configures, because accel-virt's own configure
# refuses to run without the kit, and the drivers configure decides
# there whether OpenCL.dll is built at all (only with the loader source
# present).
#
# The markers each fetch writes are declared as its byproducts, so the
# driver build below can depend on them: a pin bump that rewrites a
# tree rebuilds the drivers, and a fetch that finds nothing to do
# leaves the markers' times alone and rebuilds nothing.
set(_lsw_av_fetch_markers
    "${_lsw_av_kit}/VERSION"
    "${LSW_ACCEL_VIRT_DIR}/third_party/khronos/VERSION"
    "${LSW_ACCEL_VIRT_DIR}/third_party/opencl-loader/PINNED")
add_custom_target(accel_virt_fetch
    COMMAND ${CMAKE_COMMAND}
        -DSTEP=fetch
        "-DAV_DIR=${LSW_ACCEL_VIRT_DIR}"
        "-DKIT_DIR=${AV_WINDOWS_KIT_DIR}"
        "-DGIT=${GIT_EXECUTABLE}"
        "-DPYTHON=${Python3_EXECUTABLE}"
        -P "${_lsw_av_step}"
    BYPRODUCTS ${_lsw_av_fetch_markers}
    USES_TERMINAL
    COMMENT "accel-virt: fetching what it pins and does not commit")

# Arguments both halves configure with. clang, because accel-virt will
# take nothing else; Ninja, because its nested guest builds are Ninja
# and a Makefile parent here would gain nothing. The C compiler is
# unused today (it is a CXX project) and passed anyway, so a C source
# added there later does not quietly pick up gcc.
set(_lsw_av_cmake_args
    --no-warn-unused-cli
    -DCMAKE_C_COMPILER=${LSW_CLANG}
    -DCMAKE_CXX_COMPILER=${LSW_CLANGXX}
    -DAV_WINDOWS_KIT_DIR=${_lsw_av_kit}
    # ASan + UBSan instrument accel-virt's own test suite, which LSW
    # never builds; off, so nothing here pays for them.
    -DAV_SANITIZE=OFF
    -DAV_COVERAGE=OFF
    -DAV_FUZZ=OFF
)

# ---- the guest half: the driver package ----
# accel-virt's ``drivers`` target builds avgpu.sys and the user-mode
# drivers for both architectures, and its ``drivers`` install component
# lays them out as the guest's C:\av: provision.ps1, adapters.txt, and
# package/ with its package.sha256. Installing is what makes it a
# package — package.sha256 and adapters.txt are written by the install
# script, not the build — so the install step is the point of this.
#
# Not BUILD_ALWAYS, unlike the host half below. The guest trees compile
# against the Windows kit through a clang VFS overlay, and their depfiles
# record the overlay's virtual paths (/windows-kit/km/crt/stddef.h ...),
# which exist nowhere on disk — so Ninja finds every object in them out
# of date, every time, and asking them to build recompiles all three
# from scratch (half a minute and several CPU-minutes, on every build of
# this tree). Instead the build step depends on what those trees read:
# accel-virt's sources, its CMake, provision.ps1 and the fetch markers.
# Editing any of them, or bumping the submodule, rebuilds the drivers;
# nothing else does.
file(GLOB_RECURSE _lsw_av_guest_inputs CONFIGURE_DEPENDS
    "${LSW_ACCEL_VIRT_DIR}/src/*"
    "${LSW_ACCEL_VIRT_DIR}/samples/*"
    "${LSW_ACCEL_VIRT_DIR}/cmake/*"
    "${LSW_ACCEL_VIRT_DIR}/tools/guest/*")
list(APPEND _lsw_av_guest_inputs
    "${LSW_ACCEL_VIRT_DIR}/CMakeLists.txt"
    ${_lsw_av_fetch_markers})
ExternalProject_Add(accel_virt_drivers
    SOURCE_DIR       "${LSW_ACCEL_VIRT_DIR}"
    BINARY_DIR       "${LSW_AV_BUILD_DIR}/drivers"
    PREFIX           "${LSW_AV_BUILD_DIR}/drivers-ep"
    CMAKE_GENERATOR  Ninja
    CMAKE_ARGS       ${_lsw_av_cmake_args}
                     -DCMAKE_BUILD_TYPE=Release
                     -DCMAKE_INSTALL_PREFIX=${LSW_AV_DRIVERS_DIR}
    BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --target drivers
    BUILD_ALWAYS     OFF
    INSTALL_COMMAND  ${CMAKE_COMMAND} --install <BINARY_DIR> --component drivers
                                      --prefix ${LSW_AV_DRIVERS_DIR}
    DEPENDS          accel_virt_fetch
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD     ON
    USES_TERMINAL_INSTALL   ON
)
ExternalProject_Add_StepDependencies(accel_virt_drivers build ${_lsw_av_guest_inputs})
# The Khronos OpenCL loader, cross-built by the same guest trees once
# its source is fetched. Windows ships no OpenCL.dll, and an OpenCL
# application finds avcl.dll only through one, so it travels with the
# package: x64 for System32, x86 for SysWOW64. Packaged after the
# install, into the same tree, so the one directory is everything the
# guest needs from accel-virt.
ExternalProject_Add_Step(accel_virt_drivers loaders
    COMMAND ${CMAKE_COMMAND}
        -DSTEP=loaders
        "-DBUILD_DIR=${LSW_AV_BUILD_DIR}/drivers"
        "-DDRIVERS_DIR=${LSW_AV_DRIVERS_DIR}"
        -P "${_lsw_av_step}"
    DEPENDEES install
    BYPRODUCTS "${LSW_AV_DRIVERS_DIR}/loaders/x64/OpenCL.dll"
               "${LSW_AV_DRIVERS_DIR}/loaders/x86/OpenCL.dll"
    COMMENT "accel-virt: packaging the OpenCL loaders into av-drivers/loaders"
)

# ---- the host half: QEMU, the executor, the renderer ----
# AV_QEMU switches accel-virt to its host configuration: the default
# target then builds QEMU 11.1.1 with the device patched in (its
# ``qemu`` target), and the executor library and av-renderer beside it.
# Release, because this is what the guest runs on; unoptimized, every
# command the guest sends pays for it. Nothing is installed — QEMU runs
# from its build tree with QEMU_MODULE_DIR pointing at it.
#
# BUILD_ALWAYS here: these are host builds with honest depfiles, and the
# inner Ninja (and QEMU's own, behind build_qemu.sh) answers "nothing to
# do" in a second or two.
ExternalProject_Add(accel_virt_host
    SOURCE_DIR       "${LSW_ACCEL_VIRT_DIR}"
    BINARY_DIR       "${LSW_AV_BUILD_DIR}/qemu"
    PREFIX           "${LSW_AV_BUILD_DIR}/qemu-ep"
    CMAKE_GENERATOR  Ninja
    CMAKE_ARGS       ${_lsw_av_cmake_args}
                     -DCMAKE_BUILD_TYPE=Release
                     -DAV_QEMU=ON
    BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR>
    BUILD_ALWAYS     ON
    INSTALL_COMMAND  ""
    DEPENDS          accel_virt_fetch
    BUILD_BYPRODUCTS "${LSW_AV_BUILD_DIR}/qemu/qemu/qemu-system-x86_64"
                     "${LSW_AV_BUILD_DIR}/qemu/libav_executor.so"
                     "${LSW_AV_BUILD_DIR}/qemu/av-renderer"
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD     ON
)

# ---- the host's Direct3D ----
# accel-virt's tools/build_d3d_runtime.sh applies its patch series to
# the dxvk and vkd3d-proton submodules and builds both with meson. It
# writes to <accel-virt>/build-d3d and has no switch to write anywhere
# else. Rather than restate its meson calls and its patch-series logic
# here — which would drift the first time accel-virt changes either —
# it is run as it is, and its lib/ mirrored to where the manager looks.
# build-d3d/ is covered by accel-virt's own .gitignore (build-*/), so
# the submodule stays clean; the patch series does leave the dxvk and
# vkd3d-proton checkouts modified, as it does for anyone building
# accel-virt by hand.
#
# Without these the host serves no Direct3D and DWM crash-loops in the
# guest, so they are as much a part of the GPU as QEMU is.
add_custom_target(accel_virt_d3d ALL
    COMMAND "${LSW_ACCEL_VIRT_DIR}/tools/build_d3d_runtime.sh"
    COMMAND ${CMAKE_COMMAND}
        -DSTEP=d3d
        "-DSRC=${LSW_ACCEL_VIRT_DIR}/build-d3d/lib"
        "-DDST=${LSW_AV_BUILD_DIR}/d3d/lib"
        -P "${_lsw_av_step}"
    USES_TERMINAL
    COMMENT "accel-virt: DXVK and vkd3d-proton for the host")
add_dependencies(accel_virt_d3d accel_virt_fetch)

add_custom_target(accel_virt)
add_dependencies(accel_virt accel_virt_drivers accel_virt_host accel_virt_d3d)
