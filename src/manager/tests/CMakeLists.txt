add_executable(lsw_manager_downloader_test downloader_test.cpp)
target_link_libraries(lsw_manager_downloader_test
    PRIVATE
        LSW::Manager
        LSW::CompileOptions
        Qt6::Network
)
target_compile_definitions(lsw_manager_downloader_test PRIVATE
    QT_NO_KEYWORDS
)
add_test(NAME lsw_manager_downloader_test COMMAND lsw_manager_downloader_test)

add_executable(lsw_manager_microsoft_client_test microsoft_client_test.cpp)
target_link_libraries(lsw_manager_microsoft_client_test
    PRIVATE
        LSW::Manager
        LSW::CompileOptions
        Qt6::Network
)
target_compile_definitions(lsw_manager_microsoft_client_test PRIVATE
    QT_NO_KEYWORDS
)
add_test(NAME lsw_manager_microsoft_client_test
         COMMAND lsw_manager_microsoft_client_test)

# Standalone recorder — drives MicrosoftClient against the real
# microsoft.com and dumps responses for the test to replay. Not wired
# into ctest (it hits the network).
add_executable(lsw_manager_microsoft_client_record microsoft_client_record.cpp)
target_link_libraries(lsw_manager_microsoft_client_record
    PRIVATE
        LSW::Manager
        LSW::CompileOptions
        Qt6::Network
)
target_compile_definitions(lsw_manager_microsoft_client_record PRIVATE
    QT_NO_KEYWORDS
)
