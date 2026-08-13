# lsw_base — cross-cutting primitives every other module may depend on.
#
#   lsw/base/log.hpp      LSW_* logging macros (header-only)
#   lsw/base/logger.hpp   Logger — process-wide sink/level configuration
#
# Membership rule: no LSW domain concepts, no Qt, no platform APIs beyond
# the C++ standard library. Anything that knows what a "VM" or a "frame"
# is belongs in a higher module.

add_library(lsw_base STATIC
    src/logger.cpp
)
add_library(LSW::Base ALIAS lsw_base)

target_include_directories(lsw_base
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(lsw_base
    PUBLIC
        spdlog::spdlog
    PRIVATE
        LSW::CompileOptions
)
