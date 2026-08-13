add_executable(lsw_protocol_test buffer_test.cpp)
target_link_libraries(lsw_protocol_test PRIVATE LSW::Protocol)
add_test(NAME lsw_protocol_test COMMAND lsw_protocol_test)

# A1: one Transport, many threads. Fails without the lock.
add_executable(lsw_transport_concurrency_test transport_concurrency_test.cpp)
target_link_libraries(lsw_transport_concurrency_test PRIVATE LSW::Protocol)
target_include_directories(lsw_transport_concurrency_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lsw_transport_concurrency_test COMMAND lsw_transport_concurrency_test)
# An unsynchronised transport does not merely return wrong answers, it
# desynchronises the stream and every caller parks forever waiting for a
# reply that will never be parsed. A hang is therefore a real failure
# mode of this test and needs a bound, not patience.
set_tests_properties(lsw_transport_concurrency_test PROPERTIES TIMEOUT 60)

# B5: two processes, separate address spaces, region found by name only.
# The child re-execs this same binary with --child; that is what makes
# the address spaces genuinely independent, so it must stay an
# executable target and not be folded into another test.
add_executable(lsw_shmem_transport_test shmem_transport_test.cpp)
target_link_libraries(lsw_shmem_transport_test PRIVATE LSW::Protocol LSW::LinkProto)
target_include_directories(lsw_shmem_transport_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lsw_shmem_transport_test COMMAND lsw_shmem_transport_test)
set_tests_properties(lsw_shmem_transport_test PROPERTIES TIMEOUT 60)
