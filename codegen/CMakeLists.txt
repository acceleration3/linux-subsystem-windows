# Vulkan protocol codegen targets.
#
# The C++ output under ``generated/`` is checked into the repo so a
# normal build doesn't run Python at all. These two custom targets
# rebuild / verify it on demand:
#
#   regenerate-vulkan  — runs the codegen, overwriting ``generated/``
#   check-codegen      — runs in --check mode and fails if the output
#                        would differ (used by CI to catch stale output)
#
# Both consume ``vk.xml`` from the host's vulkan-headers package
# (Arch ``vulkan-headers``, Debian ``vulkan-headers``). If either
# Python 3.11+ or vk.xml is missing we silently omit the targets so
# people can still build without installing the codegen prerequisites.

find_file(LSW_VK_XML
    NAMES vk.xml
    PATHS /usr/share/vulkan/registry /usr/local/share/vulkan/registry
    NO_CMAKE_FIND_ROOT_PATH)

# Python 3.11+ for stdlib tomllib (used by codegen/profile.toml).
find_package(Python3 3.11 COMPONENTS Interpreter)

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Codegen targets disabled: Python 3.11+ not found")
    return()
endif()

if(NOT LSW_VK_XML)
    message(STATUS "Codegen targets disabled: vk.xml not found "
                   "(install vulkan-headers to enable regenerate-vulkan / check-codegen)")
    return()
endif()

add_custom_target(regenerate-vulkan
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_SOURCE_DIR}/codegen
            ${Python3_EXECUTABLE} -m lsw_codegen
            --xml ${LSW_VK_XML}
            --out ${CMAKE_SOURCE_DIR}/generated
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Regenerating Vulkan protocol code from ${LSW_VK_XML}"
    VERBATIM
)

add_custom_target(check-codegen
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_SOURCE_DIR}/codegen
            ${Python3_EXECUTABLE} -m lsw_codegen
            --xml ${LSW_VK_XML}
            --out ${CMAKE_SOURCE_DIR}/generated
            --check
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verifying generated/ matches the codegen output"
    VERBATIM
)
