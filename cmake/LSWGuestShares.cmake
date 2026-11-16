find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

set(LSW_VIRTIO_WIN_VERSION "0.1.302-1")
set(LSW_VIRTIO_WIN_URL
    "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-${LSW_VIRTIO_WIN_VERSION}/virtio-win-gt-x64.msi")
set(LSW_VIRTIO_WIN_SHA256 "4f4388468a7ac5286bd1f1924ef02e7281a60a600390a7cfcfd55809efc0889f")
set(LSW_VIRTIO_WIN_FILES
    "FILE_viofs_w11_amd64.inf=viofs.inf=3ecef3ce023fbb3dfea38eaedeb11706d3f5bbc5227e6c7ad207ba7f86db9208"
    "FILE_viofs_w11_amd64.sys=viofs.sys=d2bae0e61fb1927dd5a349a056dc663f04a1e680922ae60e0a61522e3a6b12ad"
    "FILE_viofs_w11_amd64.cat=viofs.cat=af4ba356fd012970006b99d90d6b0e709213a8806d9f9c36fdd39702efc8470a"
    "FILE_virtiofs_w11_amd64=virtiofs.exe=eb76b6cfbf29a3592b1adc2c02d4d8c4b622e5f3ff96707191f2eddb10356745")

set(LSW_WINFSP_VERSION "2.1.25156")
set(LSW_WINFSP_URL
    "https://github.com/winfsp/winfsp/releases/download/v2.1/winfsp-${LSW_WINFSP_VERSION}.msi")
set(LSW_WINFSP_SHA256 "073a70e00f77423e34bed98b86e600def93393ba5822204fac57a29324db9f7a")

set(LSW_GUEST_VIRTIOFS_DIR "${CMAKE_BINARY_DIR}/guest-virtiofs")
set(_lsw_shares_downloads "${CMAKE_BINARY_DIR}/_downloads")

function(_lsw_fetch_pinned url sha256 dest)
    if(EXISTS "${dest}")
        file(SHA256 "${dest}" _have)
        if(_have STREQUAL sha256)
            return()
        endif()
    endif()
    message(STATUS "Fetching ${url}")
    file(DOWNLOAD "${url}" "${dest}.part" EXPECTED_HASH SHA256=${sha256} STATUS _status TLS_VERIFY ON)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        file(REMOVE "${dest}.part")
        list(GET _status 1 _why)
        message(FATAL_ERROR "Could not fetch ${url}: ${_why}\n"
                            "The guest's shared folders and network driver need it; the build needs the network once.")
    endif()
    file(RENAME "${dest}.part" "${dest}")
endfunction()

set(_lsw_virtio_msi "${_lsw_shares_downloads}/virtio-win-gt-x64-${LSW_VIRTIO_WIN_VERSION}.msi")
set(_lsw_winfsp_msi "${_lsw_shares_downloads}/winfsp-${LSW_WINFSP_VERSION}.msi")
_lsw_fetch_pinned("${LSW_VIRTIO_WIN_URL}" "${LSW_VIRTIO_WIN_SHA256}" "${_lsw_virtio_msi}")
_lsw_fetch_pinned("${LSW_WINFSP_URL}" "${LSW_WINFSP_SHA256}" "${_lsw_winfsp_msi}")

set(LSW_VIRTIO_NET_FILES
    "FILE_netkvm_w11_amd64.inf=netkvm.inf=60f7c0ff6815b6edc7a8acf2b46b83c4605ddfbacbcd74b15c9d9653159a3a45"
    "FILE_netkvm_w11_amd64.sys=netkvm.sys=1b6f8892d39efe28c1a6b20dcc92b76f7dede567d9628fa8d6cebb62d33daf4c"
    "FILE_netkvm_w11_amd64.cat=netkvm.cat=b4c5c6c70df75777b1e5a44df8fa67bf0f2a85c016cf93f3113c0b3ab01ebc90"
    "FILE_netkvm_netkvmp_w11_amd64=netkvmp.exe=d0188e6abc35693776930a757c377067b3738d049a51ec317dbfe5214f5fef74")
set(LSW_GUEST_NETWORK_DIR "${CMAKE_BINARY_DIR}/guest-network")

function(_lsw_extract_virtio dir files pins_extra out_fresh)
    set(_stamp "${dir}/.pins")
    set(_pins "${LSW_VIRTIO_WIN_SHA256} ${pins_extra} ${files}")
    set(_current "")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _current)
    endif()
    if(_current STREQUAL _pins)
        set(${out_fresh} FALSE PARENT_SCOPE)
        return()
    endif()
    file(REMOVE_RECURSE "${dir}")
    set(_pairs "")
    foreach(_entry IN LISTS files)
        string(REPLACE "=" ";" _parts "${_entry}")
        list(GET _parts 0 _key)
        list(GET _parts 1 _name)
        list(APPEND _pairs "${_key}=${_name}")
    endforeach()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/msi_extract.py"
                "${_lsw_virtio_msi}" "${dir}" ${_pairs}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Could not take ${_pairs} out of ${_lsw_virtio_msi}")
    endif()
    foreach(_entry IN LISTS files)
        string(REPLACE "=" ";" _parts "${_entry}")
        list(GET _parts 1 _name)
        list(GET _parts 2 _want)
        file(SHA256 "${dir}/${_name}" _have)
        if(NOT _have STREQUAL _want)
            file(REMOVE_RECURSE "${dir}")
            message(FATAL_ERROR "${_name} from virtio-win ${LSW_VIRTIO_WIN_VERSION} has SHA-256 "
                                "${_have}, not the pinned ${_want}")
        endif()
    endforeach()
    file(WRITE "${_stamp}" "${_pins}")
    set(${out_fresh} TRUE PARENT_SCOPE)
endfunction()

_lsw_extract_virtio("${LSW_GUEST_VIRTIOFS_DIR}" "${LSW_VIRTIO_WIN_FILES}" "${LSW_WINFSP_SHA256}" _lsw_shares_fresh)
if(_lsw_shares_fresh)
    file(COPY_FILE "${_lsw_winfsp_msi}" "${LSW_GUEST_VIRTIOFS_DIR}/winfsp-${LSW_WINFSP_VERSION}.msi")
    message(STATUS "Guest shared folders: virtio-win ${LSW_VIRTIO_WIN_VERSION}, "
                   "WinFsp ${LSW_WINFSP_VERSION} -> ${LSW_GUEST_VIRTIOFS_DIR}")
endif()
_lsw_extract_virtio("${LSW_GUEST_NETWORK_DIR}" "${LSW_VIRTIO_NET_FILES}" "" _lsw_network_fresh)
if(_lsw_network_fresh)
    message(STATUS "Guest network driver: virtio-win ${LSW_VIRTIO_WIN_VERSION} -> ${LSW_GUEST_NETWORK_DIR}")
endif()

add_custom_command(
    OUTPUT "${LSW_GUEST_VIRTIOFS_DIR}/lsw-shares.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/guest/virtiofs/lsw-shares.ps1"
            "${LSW_GUEST_VIRTIOFS_DIR}/lsw-shares.ps1"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/guest/virtiofs/lsw-shares.ps1"
    COMMENT "Guest shared folders: lsw-shares.ps1")
add_custom_target(lsw_guest_virtiofs ALL DEPENDS "${LSW_GUEST_VIRTIOFS_DIR}/lsw-shares.ps1")
if(TARGET lsw-manager)
    add_dependencies(lsw-manager lsw_guest_virtiofs)
endif()

install(DIRECTORY "${LSW_GUEST_VIRTIOFS_DIR}/"
        DESTINATION ${LSW_INSTALL_GUESTDIR}/virtiofs
        COMPONENT ${LSW_INSTALL_COMPONENT}
        PATTERN ".pins" EXCLUDE)
install(DIRECTORY "${LSW_GUEST_NETWORK_DIR}/"
        DESTINATION ${LSW_INSTALL_GUESTDIR}/network
        COMPONENT ${LSW_INSTALL_COMPONENT}
        PATTERN ".pins" EXCLUDE)
