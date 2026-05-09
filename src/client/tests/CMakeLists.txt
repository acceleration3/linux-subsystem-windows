add_executable(lsw_icd_entry_test entry_test.cpp)
target_link_libraries(lsw_icd_entry_test
    PRIVATE
        LSW::CompileOptions
        ${CMAKE_DL_LIBS}
)
target_include_directories(lsw_icd_entry_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include
)
target_compile_definitions(lsw_icd_entry_test
    PRIVATE
        LSW_ICD_PATH="$<TARGET_FILE:lsw_icd>"
)
add_dependencies(lsw_icd_entry_test lsw_icd)
add_test(NAME lsw_icd_entry_test COMMAND lsw_icd_entry_test)
