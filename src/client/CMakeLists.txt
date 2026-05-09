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

if(NOT (UNIX AND NOT APPLE))
    return()
endif()

add_library(lsw_icd SHARED
    icd_entry.cpp
    dispatch.cpp
    handshake.cpp
    connection.cpp
    surface.cpp
    memory.cpp
    memory_shadow.cpp
    handle_unwrap.cpp
    ${CMAKE_SOURCE_DIR}/generated/vk_icd_entries.cpp
)

set_target_properties(lsw_icd PROPERTIES
    OUTPUT_NAME "lsw_icd"
    PREFIX "lib"
    SUFFIX ".so"
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
)

target_include_directories(lsw_icd
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lsw_icd
    PRIVATE
        LSW::VkProto
        LSW::Protocol
        LSW::Common
        LSW::CompileOptions
)

if(LSW_BUILD_TESTS)
    add_subdirectory(tests)
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
