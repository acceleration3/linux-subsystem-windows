add_library(lsw_protocol STATIC
    src/frame_io.cpp
    src/unix_endpoint.cpp
    src/endpoint.cpp
)
add_library(LSW::Protocol ALIAS lsw_protocol)

target_include_directories(lsw_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(lsw_protocol
    PUBLIC LSW::CompileOptions
)
