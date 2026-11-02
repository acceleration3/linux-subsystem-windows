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

set(AV_WINDOWS_KIT_DIR "" CACHE PATH
    "An already-fetched accel-virt Windows kit; empty fetches one into the submodule")
if(AV_WINDOWS_KIT_DIR)
    set(_lsw_av_kit "${AV_WINDOWS_KIT_DIR}")
else()
    set(_lsw_av_kit "${LSW_ACCEL_VIRT_DIR}/third_party/windows-kit")
endif()

set(LSW_AV_BUILD_DIR   "${CMAKE_BINARY_DIR}/accel-virt")
set(LSW_AV_PREBUILT_DRIVERS "" CACHE PATH
    "An accel-virt driver tree to install instead of building one")
if(LSW_AV_PREBUILT_DRIVERS)
    if(NOT EXISTS "${LSW_AV_PREBUILT_DRIVERS}/provision.ps1")
        message(FATAL_ERROR
            "LSW_AV_PREBUILT_DRIVERS=${LSW_AV_PREBUILT_DRIVERS} is not a driver tree (no provision.ps1)")
    endif()
    set(LSW_AV_DRIVERS_DIR "${LSW_AV_PREBUILT_DRIVERS}")
else()
    set(LSW_AV_DRIVERS_DIR "${CMAKE_BINARY_DIR}/av-drivers")
endif()
set(_lsw_av_step       "${CMAKE_CURRENT_LIST_DIR}/LSWAccelVirtStep.cmake")

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

set(_lsw_av_cmake_args
    --no-warn-unused-cli
    -DCMAKE_C_COMPILER=${LSW_CLANG}
    -DCMAKE_CXX_COMPILER=${LSW_CLANGXX}
    -DAV_WINDOWS_KIT_DIR=${_lsw_av_kit}
    -DAV_SANITIZE=OFF
    -DAV_COVERAGE=OFF
    -DAV_FUZZ=OFF
    -DAV_CHECKED=OFF
)

if(NOT LSW_AV_PREBUILT_DRIVERS)
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
endif()

ExternalProject_Add(accel_virt_host
    SOURCE_DIR       "${LSW_ACCEL_VIRT_DIR}"
    BINARY_DIR       "${LSW_AV_BUILD_DIR}/qemu"
    PREFIX           "${LSW_AV_BUILD_DIR}/qemu-ep"
    CMAKE_GENERATOR  Ninja
    CMAKE_ARGS       ${_lsw_av_cmake_args}
                     -DCMAKE_BUILD_TYPE=Release
                     -DAV_QEMU=ON
                     "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
                     "-DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}"
                     "-DCMAKE_MODULE_LINKER_FLAGS=${CMAKE_MODULE_LINKER_FLAGS}"
    BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR>
                     --target qemu av_executor av-renderer avrecord
    BUILD_ALWAYS     ON
    INSTALL_COMMAND  ""
    DEPENDS          accel_virt_fetch
    BUILD_BYPRODUCTS "${LSW_AV_BUILD_DIR}/qemu/qemu/qemu-system-x86_64"
                     "${LSW_AV_BUILD_DIR}/qemu/libav_executor.so"
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD     ON
)

add_custom_target(accel_virt_d3d ALL
    COMMAND ${CMAKE_COMMAND} -E env "LDFLAGS=${CMAKE_SHARED_LINKER_FLAGS}"
            "${LSW_ACCEL_VIRT_DIR}/tools/build_d3d_runtime.sh"
    COMMAND ${CMAKE_COMMAND}
        -DSTEP=d3d
        "-DSRC=${LSW_ACCEL_VIRT_DIR}/build-d3d/lib"
        "-DDST=${LSW_AV_BUILD_DIR}/d3d/lib"
        -P "${_lsw_av_step}"
    USES_TERMINAL
    COMMENT "accel-virt: DXVK and vkd3d-proton for the host")
add_dependencies(accel_virt_d3d accel_virt_fetch)

add_custom_target(accel_virt)
add_dependencies(accel_virt accel_virt_host accel_virt_d3d)
if(NOT LSW_AV_PREBUILT_DRIVERS)
    add_dependencies(accel_virt accel_virt_drivers)
endif()

set(_lsw_av_qemu_build "${LSW_AV_BUILD_DIR}/qemu")
set(_lsw_av_qemu_dest  "${LSW_INSTALL_LIBDIR}/accel-virt/qemu")
set(_lsw_av_exec_perms
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(PROGRAMS "${_lsw_av_qemu_build}/qemu/qemu-system-x86_64"
        DESTINATION ${_lsw_av_qemu_dest}
        COMPONENT ${LSW_INSTALL_COMPONENT})
install(FILES
            "${_lsw_av_qemu_build}/libav_executor.so"
            "${_lsw_av_qemu_build}/qemu/hw-display-virtio-gpu.so"
            "${_lsw_av_qemu_build}/qemu/hw-display-virtio-gpu-pci.so"
            "${_lsw_av_qemu_build}/qemu/hw-display-virtio-gpu-av.so"
            "${_lsw_av_qemu_build}/qemu/hw-display-virtio-gpu-pci-av.so"
            "${_lsw_av_qemu_build}/qemu/audio-pipewire.so"
            "${_lsw_av_qemu_build}/qemu/hw-usb-host.so"
        PERMISSIONS ${_lsw_av_exec_perms}
        DESTINATION ${_lsw_av_qemu_dest}
        COMPONENT ${LSW_INSTALL_COMPONENT})
install(CODE "
    file(GLOB _lsw_qemu_roms LIST_DIRECTORIES false
        \"${_lsw_av_qemu_build}/source/pc-bios/bios*.bin\"
        \"${_lsw_av_qemu_build}/source/pc-bios/vgabios*.bin\"
        \"${_lsw_av_qemu_build}/source/pc-bios/efi-*.rom\"
        \"${_lsw_av_qemu_build}/source/pc-bios/pxe-*.rom\"
        \"${_lsw_av_qemu_build}/source/pc-bios/kvmvapic.bin\"
        \"${_lsw_av_qemu_build}/source/pc-bios/linuxboot*.bin\"
        \"${_lsw_av_qemu_build}/source/pc-bios/multiboot*.bin\"
        \"${_lsw_av_qemu_build}/source/pc-bios/pvh.bin\")
    if(NOT _lsw_qemu_roms)
        message(FATAL_ERROR \"no QEMU ROMs in ${_lsw_av_qemu_build}/source/pc-bios: build accel_virt_host first\")
    endif()
    file(INSTALL \${_lsw_qemu_roms}
         DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${_lsw_av_qemu_dest}/share\")
    file(INSTALL \"${_lsw_av_qemu_build}/qemu/pc-bios/keymaps\"
         DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${_lsw_av_qemu_dest}/share\")
    " COMPONENT ${LSW_INSTALL_COMPONENT})

install(DIRECTORY "${LSW_AV_BUILD_DIR}/d3d/lib/"
        DESTINATION ${LSW_INSTALL_LIBDIR}/accel-virt/d3d
        USE_SOURCE_PERMISSIONS
        COMPONENT ${LSW_INSTALL_COMPONENT}
        FILES_MATCHING PATTERN "*.so" PATTERN "*.so.*"
        PATTERN "pkgconfig" EXCLUDE)

install(DIRECTORY "${LSW_AV_DRIVERS_DIR}/"
        DESTINATION ${LSW_INSTALL_GUESTDIR}/av-drivers
        COMPONENT ${LSW_INSTALL_COMPONENT}
        PATTERN "adapters.txt" EXCLUDE)

