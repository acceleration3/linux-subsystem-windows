add_executable(lsw_vkproto_round_trip_test round_trip_test.cpp)
target_link_libraries(lsw_vkproto_round_trip_test PRIVATE LSW::VkProto LSW::CompileOptions)
add_test(NAME lsw_vkproto_round_trip_test COMMAND lsw_vkproto_round_trip_test)

add_executable(lsw_vkproto_socket_test socket_round_trip_test.cpp)
target_link_libraries(lsw_vkproto_socket_test
    PRIVATE LSW::VkProto LSW::Server LSW::CompileOptions)
add_test(NAME lsw_vkproto_socket_test COMMAND lsw_vkproto_socket_test)
