add_executable(lsw_host_provision_downloader_test downloader_test.cpp)
target_link_libraries(lsw_host_provision_downloader_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
        Qt6::Network
)
target_compile_definitions(lsw_host_provision_downloader_test PRIVATE
    QT_NO_KEYWORDS
)
add_test(NAME lsw_host_provision_downloader_test COMMAND lsw_host_provision_downloader_test)

add_executable(lsw_host_provision_microsoft_client_test microsoft_client_test.cpp)
target_link_libraries(lsw_host_provision_microsoft_client_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
        Qt6::Network
)
target_compile_definitions(lsw_host_provision_microsoft_client_test PRIVATE
    QT_NO_KEYWORDS
)
add_test(NAME lsw_host_provision_microsoft_client_test
         COMMAND lsw_host_provision_microsoft_client_test)

# Standalone recorder — drives MicrosoftClient against the real
# microsoft.com and dumps responses for the test to replay. Not wired
# into ctest (it hits the network).
add_executable(lsw_host_provision_microsoft_client_record microsoft_client_record.cpp)
target_link_libraries(lsw_host_provision_microsoft_client_record
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
        Qt6::Network
)
target_compile_definitions(lsw_host_provision_microsoft_client_record PRIVATE
    QT_NO_KEYWORDS
)

# Guards the answer file end to end: rendered non-empty, fully
# substituted, well-formed, and non-empty once staged for the ISO.
add_executable(lsw_host_provision_autounattend_test autounattend_test.cpp)
target_link_libraries(lsw_host_provision_autounattend_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
        Qt6::Xml
)
target_compile_definitions(lsw_host_provision_autounattend_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_provision_autounattend_test
         COMMAND lsw_host_provision_autounattend_test)

# A4: the host-side half — a package in the cache becomes an unpacked
# driver directory, including the two-archives-deep layout the real
# looking-glass.io download has. No network, no VM.
add_executable(lsw_host_provision_ivshmem_driver_test ivshmem_driver_test.cpp)
target_link_libraries(lsw_host_provision_ivshmem_driver_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_provision_ivshmem_driver_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_provision_ivshmem_driver_test
         COMMAND lsw_host_provision_ivshmem_driver_test)
set_tests_properties(lsw_host_provision_ivshmem_driver_test PROPERTIES TIMEOUT 120)

# The debloater against real Windows media. Opt-in: without
# $LSW_TEST_WIM pointing at a writable install.wim copy it reports
# skipped and passes, because CI has no Windows media and the questions
# it answers — is the appx directory where we look, do the prefixes
# match what Microsoft ships, do they catch a dependency — cannot be
# answered by a fixture.
add_executable(lsw_host_provision_debloat_wim_test debloat_wim_test.cpp)
target_link_libraries(lsw_host_provision_debloat_wim_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_provision_debloat_wim_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_provision_debloat_wim_test
         COMMAND lsw_host_provision_debloat_wim_test)
# Rewriting seven gigabytes of WIM is not a sixty-second job.
set_tests_properties(lsw_host_provision_debloat_wim_test PROPERTIES TIMEOUT 3600)
