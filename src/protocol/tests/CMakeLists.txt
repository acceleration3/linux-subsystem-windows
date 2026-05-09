add_executable(lsw_protocol_test buffer_test.cpp)
target_link_libraries(lsw_protocol_test PRIVATE LSW::Protocol)
add_test(NAME lsw_protocol_test COMMAND lsw_protocol_test)
