# Shared folders, the Windows half: what the guest needs to mount the
# virtio-fs devices the domain gives it, one per shared folder
# (vm::DomainXmlBuilder). Included from the root CMakeLists for the
# manager build only.
#
# Three things from two projects, each pinned to a release and checked
# against its SHA-256, fetched at configure time like the other pinned
# downloads (cmake/LSWDependencies.cmake), never by the manager at run
# time:
#
#   virtio-win  the viofs driver (viofs.inf, .sys, .cat) and virtiofs.exe,
#               the service that serves one device as a WinFsp file
#               system. Taken out of virtio-win's guest-tools package
#               (virtio-win-gt-x64.msi, 5 MB, rather than its 900 MB ISO)
#               by cmake/msi_extract.py, the Windows 11 amd64 build of
#               each: LSW's guests are Windows 11 24H2 or later, which
#               accel-virt needs. Each file is checked against its own
#               pinned hash too. BSD-3-Clause.
#   WinFsp      its installer, unmodified, as the WinFsp project releases
#               it. GPLv3 with the FLOSS exception, which allows exactly
#               this (packaging/licenses/winfsp-LICENSE).
#
# and LSW's own src/guest/virtiofs/lsw-shares.ps1, which installs and
# mounts them in the guest. Together they are the tree
#
#   ${CMAKE_BINARY_DIR}/guest-virtiofs/          LSW_GUEST_VIRTIOFS
#       viofs.inf viofs.sys viofs.cat virtiofs.exe
#       winfsp-<version>.msi lsw-shares.ps1
#
# installed as share/lsw/guest/virtiofs, which the manager finds the way
# it finds av-drivers (platform::GuestVirtiofsPath), stages on the
# payload ISO for a new install (C:\ProgramData\LSW\virtiofs) and copies
# into an existing guest (services::GuestBundle).
#
# To move a pin: change the version and the package's hash, configure,
# and take the extracted files' new hashes from the error it stops with.

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

set(LSW_VIRTIO_WIN_VERSION "0.1.302-1")
set(LSW_VIRTIO_WIN_URL
    "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-${LSW_VIRTIO_WIN_VERSION}/virtio-win-gt-x64.msi")
set(LSW_VIRTIO_WIN_SHA256 "4f4388468a7ac5286bd1f1924ef02e7281a60a600390a7cfcfd55809efc0889f")
# The package's key for each file -> its name in the tree, and its hash.
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

# A pinned file, downloaded unless it is already there with its hash.
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
                            "The guest's shared folders need it; the build needs the network once.")
    endif()
    file(RENAME "${dest}.part" "${dest}")
endfunction()

set(_lsw_virtio_msi "${_lsw_shares_downloads}/virtio-win-gt-x64-${LSW_VIRTIO_WIN_VERSION}.msi")
set(_lsw_winfsp_msi "${_lsw_shares_downloads}/winfsp-${LSW_WINFSP_VERSION}.msi")
_lsw_fetch_pinned("${LSW_VIRTIO_WIN_URL}" "${LSW_VIRTIO_WIN_SHA256}" "${_lsw_virtio_msi}")
_lsw_fetch_pinned("${LSW_WINFSP_URL}" "${LSW_WINFSP_SHA256}" "${_lsw_winfsp_msi}")

# The tree, rebuilt whole when a pin moved so no old file stays behind.
set(_lsw_shares_stamp "${LSW_GUEST_VIRTIOFS_DIR}/.pins")
set(_lsw_shares_pins "${LSW_VIRTIO_WIN_SHA256} ${LSW_WINFSP_SHA256} ${LSW_VIRTIO_WIN_FILES}")
set(_lsw_shares_current "")
if(EXISTS "${_lsw_shares_stamp}")
    file(READ "${_lsw_shares_stamp}" _lsw_shares_current)
endif()
if(NOT _lsw_shares_current STREQUAL _lsw_shares_pins)
    file(REMOVE_RECURSE "${LSW_GUEST_VIRTIOFS_DIR}")
    set(_lsw_pairs "")
    foreach(_entry IN LISTS LSW_VIRTIO_WIN_FILES)
        string(REPLACE "=" ";" _parts "${_entry}")
        list(GET _parts 0 _key)
        list(GET _parts 1 _name)
        list(APPEND _lsw_pairs "${_key}=${_name}")
    endforeach()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/msi_extract.py"
                "${_lsw_virtio_msi}" "${LSW_GUEST_VIRTIOFS_DIR}" ${_lsw_pairs}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Could not take the viofs driver out of ${_lsw_virtio_msi}")
    endif()
    foreach(_entry IN LISTS LSW_VIRTIO_WIN_FILES)
        string(REPLACE "=" ";" _parts "${_entry}")
        list(GET _parts 1 _name)
        list(GET _parts 2 _want)
        file(SHA256 "${LSW_GUEST_VIRTIOFS_DIR}/${_name}" _have)
        if(NOT _have STREQUAL _want)
            file(REMOVE_RECURSE "${LSW_GUEST_VIRTIOFS_DIR}")
            message(FATAL_ERROR "${_name} from virtio-win ${LSW_VIRTIO_WIN_VERSION} has SHA-256 "
                                "${_have}, not the pinned ${_want}")
        endif()
    endforeach()
    file(COPY_FILE "${_lsw_winfsp_msi}" "${LSW_GUEST_VIRTIOFS_DIR}/winfsp-${LSW_WINFSP_VERSION}.msi")
    file(WRITE "${_lsw_shares_stamp}" "${_lsw_shares_pins}")
    message(STATUS "Guest shared folders: virtio-win ${LSW_VIRTIO_WIN_VERSION}, "
                   "WinFsp ${LSW_WINFSP_VERSION} -> ${LSW_GUEST_VIRTIOFS_DIR}")
endif()

# LSW's own script, copied at build time so an edit reaches the tree
# without a configure.
add_custom_command(
    OUTPUT "${LSW_GUEST_VIRTIOFS_DIR}/lsw-shares.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/guest/virtiofs/lsw-shares.ps1"
            "${LSW_GUEST_VIRTIOFS_DIR}/lsw-shares.ps1"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/guest/virtiofs/lsw-shares.ps1"
    COMMENT "Guest shared folders: lsw-shares.ps1")
add_custom_target(lsw_guest_virtiofs ALL DEPENDS "${LSW_GUEST_VIRTIOFS_DIR}/lsw-shares.ps1")
# The manager is what uses the tree, and scripts/build.sh builds it by
# name: building it brings the tree up to date.
if(TARGET lsw-manager)
    add_dependencies(lsw-manager lsw_guest_virtiofs)
endif()

install(DIRECTORY "${LSW_GUEST_VIRTIOFS_DIR}/"
        DESTINATION ${LSW_INSTALL_GUESTDIR}/virtiofs
        COMPONENT ${LSW_INSTALL_COMPONENT}
        PATTERN ".pins" EXCLUDE)
