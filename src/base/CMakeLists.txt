add_library(lsw_base STATIC
    src/crash_handler.cpp
    src/log_format.cpp
    src/log_tail.cpp
    src/logger.cpp
    src/repeat_collapser.cpp
    src/rolling_file.cpp
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
