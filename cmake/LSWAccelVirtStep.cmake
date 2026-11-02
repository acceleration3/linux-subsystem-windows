cmake_minimum_required(VERSION 3.25)

if(STEP STREQUAL "fetch")
    if(NOT EXISTS "${AV_DIR}/third_party/dxvk/meson.build"
       OR NOT EXISTS "${AV_DIR}/third_party/vkd3d-proton/meson.build")
        message(STATUS "accel-virt: initialising third_party/dxvk and third_party/vkd3d-proton")
        execute_process(
            COMMAND "${GIT}" -C "${AV_DIR}" submodule update --init --recursive
                    third_party/dxvk third_party/vkd3d-proton
            COMMAND_ERROR_IS_FATAL ANY)
    endif()

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

    execute_process(COMMAND "${PYTHON}" -B "${AV_DIR}/tools/fetch_khronos_registries.py"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${PYTHON}" -B "${AV_DIR}/tools/fetch_opencl_loader.py"
                    COMMAND_ERROR_IS_FATAL ANY)
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
