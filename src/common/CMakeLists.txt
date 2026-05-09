# lsw_common: small shared utilities pulled in by every subproject.
# Today it's just the spdlog-backed logger; future cross-cutting helpers
# (env-var parsing, signal handling) land here too.

add_library(lsw_common STATIC
    src/log.cpp
)
add_library(LSW::Common ALIAS lsw_common)

target_include_directories(lsw_common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(lsw_common
    PUBLIC
        spdlog::spdlog
    PRIVATE
        LSW::CompileOptions
)
