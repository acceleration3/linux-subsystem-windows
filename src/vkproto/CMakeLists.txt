# lsw_vkproto: the Vulkan-aware protocol library.
#
# Built from `generated/` (output of codegen) plus a small loopback transport
# helper used by tests. Depends on Vulkan-Headers (header-only) and lsw_protocol.

set(GENERATED_DIR ${CMAKE_SOURCE_DIR}/generated)

# Sanity check: refuse to configure if codegen hasn't been run.
foreach(f vk_protocol_ids.h vk_serialize.h vk_dispatch_client.h vk_dispatch_client.cpp
          vk_dispatch_server.h vk_dispatch_server.cpp
          vk_command_names.h vk_command_names.cpp
          vk_handle_default.cpp)
    if(NOT EXISTS ${GENERATED_DIR}/${f})
        message(FATAL_ERROR
            "Missing generated file: ${GENERATED_DIR}/${f}\n"
            "Run: cmake --build <builddir> --target regenerate-vulkan\n"
            "  or: python -m lsw_codegen --xml ... --out generated/")
    endif()
endforeach()

add_library(lsw_vkproto STATIC
    ${GENERATED_DIR}/vk_dispatch_client.cpp
    ${GENERATED_DIR}/vk_dispatch_server.cpp
    ${GENERATED_DIR}/vk_command_names.cpp
    ${GENERATED_DIR}/vk_handle_default.cpp
    src/loopback.cpp
)
add_library(LSW::VkProto ALIAS lsw_vkproto)

target_include_directories(lsw_vkproto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${GENERATED_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include>
)

target_link_libraries(lsw_vkproto
    PUBLIC LSW::Protocol LSW::Common
    PRIVATE LSW::CompileOptions
)

if(LSW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
