add_library(lsw_linkproto INTERFACE)
add_library(LSW::LinkProto ALIAS lsw_linkproto)

target_include_directories(lsw_linkproto
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(lsw_linkproto INTERFACE cxx_std_20)
