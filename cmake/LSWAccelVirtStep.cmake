# The script-mode halves of the accel-virt targets (cmake -P), one per
# STEP. See LSWAccelVirt.cmake for what each target is for; this file is
# only the how.
#
#   STEP=fetch    AV_DIR, KIT_DIR (empty: fetch into the submodule), GIT, PYTHON
#   STEP=loaders  BUILD_DIR (accel-virt's drivers build), DRIVERS_DIR (av-drivers)
#   STEP=d3d      SRC (accel-virt's build-d3d/lib), DST

cmake_minimum_required(VERSION 3.25)

if(STEP STREQUAL "fetch")
    # The dxvk and vkd3d-proton submodules: both halves compile against
    # DXVK's native Direct3D headers, so they are needed before either
    # configures, not only by the Direct3D build. Initialised once;
    # after that build_d3d_runtime.sh keeps them at their pins itself.
    if(NOT EXISTS "${AV_DIR}/third_party/dxvk/meson.build"
       OR NOT EXISTS "${AV_DIR}/third_party/vkd3d-proton/meson.build")
        message(STATUS "accel-virt: initialising third_party/dxvk and third_party/vkd3d-proton")
        execute_process(
            COMMAND "${GIT}" -C "${AV_DIR}" submodule update --init --recursive
                    third_party/dxvk third_party/vkd3d-proton
            COMMAND_ERROR_IS_FATAL ANY)
    endif()

    # A kit supplied from elsewhere is the user's to keep current;
    # accel-virt's configure checks its VERSION against the pin and
    # says so if it is stale.
    if(KIT_DIR)
        if(NOT EXISTS "${KIT_DIR}/VERSION")
            message(FATAL_ERROR
                "AV_WINDOWS_KIT_DIR=${KIT_DIR} holds no fetched Windows kit (no VERSION).\n"
                "Point it at an accel-virt third_party/windows-kit, or clear it to fetch one.")
        endif()
    else()
        execute_process(COMMAND bash "${AV_DIR}/tools/fetch_windows_kit.sh"
                        COMMAND_ERROR_IS_FATAL ANY)
    endif()

    # vk.xml, gl.xml, cl.xml and the Khronos headers: without them the
    # generated wire code is left out of the build, and the executor
    # with it.
    execute_process(COMMAND "${PYTHON}" -B "${AV_DIR}/tools/fetch_khronos_registries.py"
                    COMMAND_ERROR_IS_FATAL ANY)
    # The Khronos OpenCL loader's source. The guest builds make
    # OpenCL.dll from it, and only when it is there at their configure.
    execute_process(COMMAND "${PYTHON}" -B "${AV_DIR}/tools/fetch_opencl_loader.py"
                    COMMAND_ERROR_IS_FATAL ANY)
    # QEMU 11.1.1's release tarball. build_qemu.sh would fetch it too;
    # doing it here keeps every download in one step, before anything
    # spends an hour compiling.
    execute_process(COMMAND bash "${AV_DIR}/tools/fetch_qemu.sh"
                    COMMAND_ERROR_IS_FATAL ANY)

elseif(STEP STREQUAL "loaders")
    foreach(pair "guest;x64" "guest32;x86")
        list(GET pair 0 tree)
        list(GET pair 1 arch)
        set(loader "${BUILD_DIR}/${tree}/OpenCL.dll")
        if(NOT EXISTS "${loader}")
            message(FATAL_ERROR
                "accel-virt built no ${tree}/OpenCL.dll in ${BUILD_DIR}.\n"
                "It is built only when third_party/opencl-loader was fetched before the "
                "guest trees configured (tools/fetch_opencl_loader.py). Delete "
                "${BUILD_DIR} to configure them again.")
        endif()
        file(MAKE_DIRECTORY "${DRIVERS_DIR}/loaders/${arch}")
        file(COPY_FILE "${loader}" "${DRIVERS_DIR}/loaders/${arch}/OpenCL.dll"
             ONLY_IF_DIFFERENT)
        message(STATUS "Installing: ${DRIVERS_DIR}/loaders/${arch}/OpenCL.dll")
    endforeach()

elseif(STEP STREQUAL "d3d")
    if(NOT IS_DIRECTORY "${SRC}")
        message(FATAL_ERROR "build_d3d_runtime.sh left no ${SRC}")
    endif()
    # A mirror, not an accumulation: a library DXVK no longer installs
    # must not linger for the executor to find. file(COPY) keeps the
    # .so -> .so.0 -> .so.0.x symlinks as symlinks and skips what is
    # already up to date.
    file(GLOB stale RELATIVE "${DST}" "${DST}/*")
    foreach(name IN LISTS stale)
        if(NOT EXISTS "${SRC}/${name}" AND NOT IS_SYMLINK "${SRC}/${name}")
            file(REMOVE_RECURSE "${DST}/${name}")
        endif()
    endforeach()
    file(MAKE_DIRECTORY "${DST}")
    file(COPY "${SRC}/" DESTINATION "${DST}")

else()
    message(FATAL_ERROR "LSWAccelVirtStep.cmake: unknown STEP '${STEP}'")
endif()
