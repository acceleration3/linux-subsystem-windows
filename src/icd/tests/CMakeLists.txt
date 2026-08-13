add_executable(lsw_icd_entry_test entry_test.cpp)
target_link_libraries(lsw_icd_entry_test
    PRIVATE
        LSW::CompileOptions
        ${CMAKE_DL_LIBS}
)
target_link_libraries(lsw_icd_entry_test PRIVATE Vulkan::Headers)
target_compile_definitions(lsw_icd_entry_test
    PRIVATE
        LSW_ICD_PATH="$<TARGET_FILE:lsw_icd>"
)
add_dependencies(lsw_icd_entry_test lsw_icd)
add_test(NAME lsw_icd_entry_test COMMAND lsw_icd_entry_test)

# Phase C: N threads record concurrently into their own command buffers,
# and the whole buffer ships as one message. Compiles the recorder
# directly rather than linking the ICD .so — the test is about the
# accumulate/unpack pair, not about loading a driver.
add_executable(lsw_icd_batch_test
    batch_test.cpp
    ../src/command_recorder.cpp
)
target_include_directories(lsw_icd_batch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(lsw_icd_batch_test
    PRIVATE
        LSW::CompileOptions
        LSW::Protocol
        LSW::Base
        lsw_render
        Vulkan::Headers
)
add_test(NAME lsw_icd_batch_test COMMAND lsw_icd_batch_test)
set_tests_properties(lsw_icd_batch_test PROPERTIES TIMEOUT 60)
