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

add_executable(lsw_host_media_language_test media_language_test.cpp)
target_link_libraries(lsw_host_media_language_test PRIVATE lsw_host_provision LSW::CompileOptions)
target_compile_definitions(lsw_host_media_language_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_media_language_test COMMAND lsw_host_media_language_test)

add_executable(lsw_host_no_prompt_media_test no_prompt_media_test.cpp)
target_link_libraries(lsw_host_no_prompt_media_test PRIVATE lsw_host_provision LSW::CompileOptions)
target_compile_definitions(lsw_host_no_prompt_media_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_no_prompt_media_test COMMAND lsw_host_no_prompt_media_test)

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

add_executable(lsw_host_provision_debloat_wim_test debloat_wim_test.cpp)
target_link_libraries(lsw_host_provision_debloat_wim_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_provision_debloat_wim_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_provision_debloat_wim_test
         COMMAND lsw_host_provision_debloat_wim_test)
set_tests_properties(lsw_host_provision_debloat_wim_test PROPERTIES TIMEOUT 3600)

add_executable(lsw_host_link_test link_test.cpp)
target_link_libraries(lsw_host_link_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
        nlohmann_json::nlohmann_json
)
target_compile_definitions(lsw_host_link_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_link_test COMMAND lsw_host_link_test)

add_executable(lsw_host_domain_xml_test domain_xml_test.cpp)
target_link_libraries(lsw_host_domain_xml_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
        Qt6::Xml
)
target_compile_definitions(lsw_host_domain_xml_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_domain_xml_test COMMAND lsw_host_domain_xml_test)

add_executable(lsw_host_adapter_names_test adapter_names_test.cpp)
target_link_libraries(lsw_host_adapter_names_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_adapter_names_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_adapter_names_test COMMAND lsw_host_adapter_names_test)

add_executable(lsw_host_driver_catalog_test driver_catalog_test.cpp)
target_link_libraries(lsw_host_driver_catalog_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_driver_catalog_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_driver_catalog_test COMMAND lsw_host_driver_catalog_test)

add_executable(lsw_host_gpus_test host_gpus_test.cpp)
target_link_libraries(lsw_host_gpus_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_gpus_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_gpus_test COMMAND lsw_host_gpus_test)

add_executable(lsw_host_display_modes_test display_modes_test.cpp)
target_link_libraries(lsw_host_display_modes_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_display_modes_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_display_modes_test COMMAND lsw_host_display_modes_test)

add_executable(lsw_host_display_refresh_test display_refresh_test.cpp)
target_link_libraries(lsw_host_display_refresh_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_display_refresh_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_display_refresh_test COMMAND lsw_host_display_refresh_test)

add_executable(lsw_host_domain_description_test domain_description_test.cpp)
target_link_libraries(lsw_host_domain_description_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_domain_description_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_domain_description_test COMMAND lsw_host_domain_description_test)

add_executable(lsw_host_disk_grow_test disk_grow_test.cpp)
target_link_libraries(lsw_host_disk_grow_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_disk_grow_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_disk_grow_test COMMAND lsw_host_disk_grow_test)

add_executable(lsw_host_guest_sessions_test guest_sessions_test.cpp)
target_link_libraries(lsw_host_guest_sessions_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_guest_sessions_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_sessions_test COMMAND lsw_host_guest_sessions_test)

add_executable(lsw_host_guest_gpu_status_test guest_gpu_status_test.cpp)
target_link_libraries(lsw_host_guest_gpu_status_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_guest_gpu_status_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_gpu_status_test COMMAND lsw_host_guest_gpu_status_test)

add_executable(lsw_host_guest_swap_test guest_swap_test.cpp)
target_link_libraries(lsw_host_guest_swap_test
    PRIVATE lsw_host_manager LSW::CompileOptions)
target_compile_definitions(lsw_host_guest_swap_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_swap_test COMMAND lsw_host_guest_swap_test)

add_executable(lsw_host_guest_app_launcher_test guest_app_launcher_test.cpp)
target_link_libraries(lsw_host_guest_app_launcher_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_guest_app_launcher_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_app_launcher_test COMMAND lsw_host_guest_app_launcher_test)
set_tests_properties(lsw_host_guest_app_launcher_test PROPERTIES TIMEOUT 30)

add_executable(lsw_host_guest_file_transfer_test
    guest_file_transfer_test.cpp
    ../../agent/src/apps/upload_store.cpp
    ../../agent/src/util/base64.cpp)
target_include_directories(lsw_host_guest_file_transfer_test PRIVATE ../../agent/include)
target_link_libraries(lsw_host_guest_file_transfer_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_guest_file_transfer_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_file_transfer_test COMMAND lsw_host_guest_file_transfer_test)
set_tests_properties(lsw_host_guest_file_transfer_test PROPERTIES TIMEOUT 60)

if(TARGET rwin_link)
    add_executable(lsw_host_desktop_preview_test desktop_preview_test.cpp)
    target_link_libraries(lsw_host_desktop_preview_test
        PRIVATE lsw_host_manager rwin::link LSW::CompileOptions nlohmann_json::nlohmann_json)
    target_compile_definitions(lsw_host_desktop_preview_test PRIVATE QT_NO_KEYWORDS)
    add_test(NAME lsw_host_desktop_preview_test COMMAND lsw_host_desktop_preview_test)
    set_tests_properties(lsw_host_desktop_preview_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()

add_executable(lsw_host_store_app_dialog_test store_app_dialog_test.cpp)
target_link_libraries(lsw_host_store_app_dialog_test
    PRIVATE lsw_host_manager LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_store_app_dialog_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_store_app_dialog_test COMMAND lsw_host_store_app_dialog_test)
set_tests_properties(lsw_host_store_app_dialog_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lsw_host_notice_board_test notice_board_test.cpp)
target_link_libraries(lsw_host_notice_board_test PRIVATE lsw_host_manager LSW::CompileOptions)
target_compile_definitions(lsw_host_notice_board_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_notice_board_test COMMAND lsw_host_notice_board_test)
set_tests_properties(lsw_host_notice_board_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lsw_host_usb_devices_test host_usb_devices_test.cpp)
target_link_libraries(lsw_host_usb_devices_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_usb_devices_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_usb_devices_test COMMAND lsw_host_usb_devices_test)

add_executable(lsw_host_guest_document_path_test guest_document_path_test.cpp)
target_link_libraries(lsw_host_guest_document_path_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_guest_document_path_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_document_path_test COMMAND lsw_host_guest_document_path_test)

add_executable(lsw_host_mime_defaults_test mime_defaults_test.cpp)
target_link_libraries(lsw_host_mime_defaults_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_mime_defaults_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_mime_defaults_test COMMAND lsw_host_mime_defaults_test)

add_executable(lsw_host_guest_bundle_test guest_bundle_test.cpp)
target_link_libraries(lsw_host_guest_bundle_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_guest_bundle_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_bundle_test COMMAND lsw_host_guest_bundle_test)
set_tests_properties(lsw_host_guest_bundle_test PROPERTIES TIMEOUT 60)

add_executable(lsw_host_update_verdict_test update_verdict_test.cpp)
target_link_libraries(lsw_host_update_verdict_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_update_verdict_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_update_verdict_test COMMAND lsw_host_update_verdict_test)

add_executable(lsw_host_update_batch_test update_batch_test.cpp)
target_link_libraries(lsw_host_update_batch_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_update_batch_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_update_batch_test COMMAND lsw_host_update_batch_test)

add_executable(lsw_host_payload_iso_test payload_iso_test.cpp)
target_link_libraries(lsw_host_payload_iso_test PRIVATE lsw_host_provision LSW::CompileOptions)
target_compile_definitions(lsw_host_payload_iso_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_payload_iso_test COMMAND lsw_host_payload_iso_test)

add_executable(lsw_host_guest_upkeep_test guest_upkeep_test.cpp)
target_link_libraries(lsw_host_guest_upkeep_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_guest_upkeep_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_upkeep_test COMMAND lsw_host_guest_upkeep_test)
set_tests_properties(lsw_host_guest_upkeep_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_executable(lsw_host_update_dialog_test update_dialog_test.cpp)
target_link_libraries(lsw_host_update_dialog_test PRIVATE lsw_host_manager LSW::CompileOptions)
target_compile_definitions(lsw_host_update_dialog_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_update_dialog_test COMMAND lsw_host_update_dialog_test)
set_tests_properties(lsw_host_update_dialog_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lsw_host_logging_test logging_test.cpp)
target_link_libraries(lsw_host_logging_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_logging_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_logging_test COMMAND lsw_host_logging_test)
set_tests_properties(lsw_host_logging_test PROPERTIES TIMEOUT 60)

add_executable(lsw_host_log_page_test log_page_test.cpp)
target_link_libraries(lsw_host_log_page_test PRIVATE lsw_host_manager LSW::CompileOptions)
target_compile_definitions(lsw_host_log_page_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_log_page_test COMMAND lsw_host_log_page_test)
set_tests_properties(lsw_host_log_page_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

add_executable(lsw_host_agent_files_test agent_files_test.cpp)
target_link_libraries(lsw_host_agent_files_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_agent_files_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_agent_files_test COMMAND lsw_host_agent_files_test)
set_tests_properties(lsw_host_agent_files_test PROPERTIES TIMEOUT 60)

add_executable(lsw_host_log_collector_test log_collector_test.cpp)
target_link_libraries(lsw_host_log_collector_test PRIVATE lsw_host_core LSW::CompileOptions ZLIB::ZLIB)
target_compile_definitions(lsw_host_log_collector_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_log_collector_test COMMAND lsw_host_log_collector_test)
set_tests_properties(lsw_host_log_collector_test PROPERTIES TIMEOUT 120)

add_executable(lsw_host_log_cleaner_test log_cleaner_test.cpp)
target_link_libraries(lsw_host_log_cleaner_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_log_cleaner_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_log_cleaner_test COMMAND lsw_host_log_cleaner_test)
set_tests_properties(lsw_host_log_cleaner_test PROPERTIES TIMEOUT 120)

add_executable(lsw_host_av_state_dump_test av_state_dump_test.cpp)
target_link_libraries(lsw_host_av_state_dump_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_av_state_dump_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_av_state_dump_test COMMAND lsw_host_av_state_dump_test)

add_executable(lsw_host_host_window_status_test host_window_status_test.cpp)
target_link_libraries(lsw_host_host_window_status_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_host_window_status_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_host_window_status_test COMMAND lsw_host_host_window_status_test)

add_executable(lsw_host_qemu_process_test qemu_process_test.cpp)
target_link_libraries(lsw_host_qemu_process_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_qemu_process_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_qemu_process_test COMMAND lsw_host_qemu_process_test)

add_executable(lsw_host_host_window_alerts_test host_window_alerts_test.cpp)
target_link_libraries(lsw_host_host_window_alerts_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_host_window_alerts_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_host_window_alerts_test COMMAND lsw_host_host_window_alerts_test)

add_executable(lsw_host_table_header_test table_header_test.cpp)
target_link_libraries(lsw_host_table_header_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_table_header_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_table_header_test COMMAND lsw_host_table_header_test)
set_tests_properties(lsw_host_table_header_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_executable(lsw_host_vram_reserve_test vram_reserve_test.cpp)
target_link_libraries(lsw_host_vram_reserve_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_vram_reserve_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_vram_reserve_test COMMAND lsw_host_vram_reserve_test)
