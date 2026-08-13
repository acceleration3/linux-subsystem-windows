# Linux client ICD.
#
# The ICD is a single .so the Vulkan loader discovers via a JSON manifest.
# Per-command wire wrappers are generated (see generated/vk_icd_entries.*);
# this directory owns:
#   icd_entry.cpp     loader negotiation: vk_icdGetInstanceProcAddr,
#                     vk_icdNegotiateLoaderICDInterfaceVersion, the local
#                     EnumerateInstance{Version,ExtensionProperties,
#                     LayerProperties} no-ops, and Get{Instance,Device}ProcAddr.
#   dispatch.cpp      name → entry-point lookup glue.
#   handshake.cpp     HELLO/HELLO_ACK exchange used by Connection::open.
#   connection.cpp    per-VkInstance Connection (owns a Transport + state).
#   queue.cpp         submit-time flush of mapped memory (HOST_COHERENT
#                     writes have no flush call to hook, and there is no
#                     coherence across the process boundary).

# The same sources build ``liblsw_icd.so`` on Linux and ``lsw_icd.dll``
# under mingw. Not "an equivalent driver for Windows" — the same
# translation units, the same wire format, the same batching, the same
# transport. What differs is the file extension and, one layer down,
# which SharedRegion maps the pages.
#
# The Windows build became possible rather than merely desirable once
# Phase B made the shared-memory channel the portable one: nothing here
# needs a POSIX socket any more.
add_library(lsw_icd SHARED
    src/icd_entry.cpp
    src/dispatch.cpp
    src/handshake.cpp
    src/connection.cpp
    src/surface.cpp
    src/memory.cpp
    src/queue.cpp
    src/memory_shadow.cpp
    src/handle_unwrap.cpp
    src/cmdbuf.cpp
    src/command_recorder.cpp
    ${CMAKE_SOURCE_DIR}/generated/vk_icd_entries.cpp
)

if(WIN32)
    set_target_properties(lsw_icd PROPERTIES
        OUTPUT_NAME "lsw_icd"
        PREFIX ""
        SUFFIX ".dll"
    )
    # The loader looks up entry points by name through
    # vk_icdGetInstanceProcAddr, so nothing needs exporting beyond the
    # handful the ICD interface names. They carry the attribute
    # themselves; this keeps mingw from also exporting everything else.
    set_target_properties(lsw_icd PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        CXX_VISIBILITY_PRESET hidden
    )
else()
    set_target_properties(lsw_icd PROPERTIES
        OUTPUT_NAME "lsw_icd"
        PREFIX "lib"
        SUFFIX ".so"
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
    )
endif()

target_include_directories(lsw_icd
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lsw_icd
    PRIVATE
        LSW::VkProto
        LSW::Protocol
        LSW::Base
        LSW::CompileOptions
)

if(LSW_BUILD_TESTS AND NOT WIN32)
    add_subdirectory(tests)
endif()

# The Windows build stops here: the JSON manifest below is the Linux
# loader's discovery mechanism. Windows registers an ICD through
# ``HKLM\SOFTWARE\Khronos\Vulkan\Drivers`` instead, which is E2's job
# because the registry value has to exist before anything runs.
if(WIN32)
    return()
endif()

# Concretize the ICD JSON manifest next to the .so for development use.
# Two-pass: configure_file does ``@LSW_ICD_INSTALL_PATH@`` substitution at
# configure time, file(GENERATE) resolves the embedded ``$<TARGET_FILE:...>``
# generator expression at generate time.
set(LSW_ICD_INSTALL_PATH "$<TARGET_FILE:lsw_icd>")
configure_file(
    ${CMAKE_SOURCE_DIR}/packaging/linux/lsw_icd.json.in
    ${CMAKE_BINARY_DIR}/lsw_icd.json.tmp
    @ONLY
)
file(GENERATE
    OUTPUT ${CMAKE_BINARY_DIR}/lsw_icd.json
    INPUT ${CMAKE_BINARY_DIR}/lsw_icd.json.tmp
)
