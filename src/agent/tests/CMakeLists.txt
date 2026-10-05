# Host-native tests for the agent's platform-free logic.
#
# The agent executable itself only builds under a Windows toolchain, so
# this target does NOT link it. It compiles the handful of agent TUs that
# touch nothing but the standard library, directly, with the host
# compiler — which is why it can run in a normal `ctest` pass on Linux.
#
# Membership rule for this list: if the TU includes <windows.h>, it does
# not belong here.
add_executable(lsw_agent_logic_test
    agent_logic_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/util/base64.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/install/url_file_name.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/install/job_id_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/util/wide_string.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/rpc/params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/command_line.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/session/session_selector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/session/session_routing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/session/pipe_codec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/session/pending_calls.cpp
)

target_include_directories(lsw_agent_logic_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(lsw_agent_logic_test PRIVATE
    LSW::CompileOptions
    LSW::LinkProto   # the envelope the service <-> helper pipe reuses
    nlohmann_json::nlohmann_json)

add_test(NAME lsw_agent_logic_test COMMAND lsw_agent_logic_test)

add_executable(lsw_agent_upload_test
    upload_test.cpp
    ../src/apps/upload_store.cpp
    ../src/apps/launch_command.cpp
    ../src/util/base64.cpp
    ../src/util/wide_string.cpp)
target_include_directories(lsw_agent_upload_test PRIVATE ../include)
target_link_libraries(lsw_agent_upload_test PRIVATE LSW::CompileOptions LSW::LinkProto)
add_test(NAME lsw_agent_upload_test COMMAND lsw_agent_upload_test)
