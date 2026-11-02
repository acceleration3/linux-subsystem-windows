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
# The languages a Windows medium carries, which the answer file may name.
add_executable(lsw_host_media_language_test media_language_test.cpp)
target_link_libraries(lsw_host_media_language_test PRIVATE lsw_host_provision LSW::CompileOptions)
target_compile_definitions(lsw_host_media_language_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_media_language_test COMMAND lsw_host_media_language_test)

# Writes a small El Torito ISO and runs 7-Zip and qemu-img over it, as
# the install does over the Windows ISO.
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

# The agent channel's bookkeeping — reply decoding, reply-to-caller
# matching, event subscriptions — with no agent and no socket.
add_executable(lsw_host_link_test link_test.cpp)
target_link_libraries(lsw_host_link_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
        nlohmann_json::nlohmann_json
)
target_compile_definitions(lsw_host_link_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_link_test COMMAND lsw_host_link_test)

# The domain definition with user-chosen paths in it: escaped, parsed
# back, and built from settings the same way every time. Core only — no
# libvirt, no VM.
add_executable(lsw_host_domain_xml_test domain_xml_test.cpp)
target_link_libraries(lsw_host_domain_xml_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
        Qt6::Xml
)
target_compile_definitions(lsw_host_domain_xml_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_domain_xml_test COMMAND lsw_host_domain_xml_test)

# Which host GPU "auto" or a UUID names, agreeing with accel-virt's
# device. Pure over a list; also prints what this host enumerates.
# The guest's adapter names, written into the payload on the user's
# machine: accel-virt's naming rules against a fake sysfs and fake ID
# databases.
add_executable(lsw_host_adapter_names_test adapter_names_test.cpp)
target_link_libraries(lsw_host_adapter_names_test
    PRIVATE
        lsw_host_provision
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_adapter_names_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_adapter_names_test COMMAND lsw_host_adapter_names_test)

# The Update Catalog's answer read for a GPU's current driver version, and
# the lookup over a fake fetch: no network.
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

# The guest's resolution list: the driver's limits and the stored and
# sent forms.
add_executable(lsw_host_display_modes_test display_modes_test.cpp)
target_link_libraries(lsw_host_display_modes_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_display_modes_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_display_modes_test COMMAND lsw_host_display_modes_test)

# The guest's refresh rate: the setting, a mode's exact rate, and the list
# of the monitors' rates the settings offer.
add_executable(lsw_host_display_refresh_test display_refresh_test.cpp)
target_link_libraries(lsw_host_display_refresh_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_display_refresh_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_display_refresh_test COMMAND lsw_host_display_refresh_test)

# The domain definition read back, which is where the running machine's
# status comes from: the virtio-gpu-av-pci argument parser against the
# builder's output and libvirt's live shape, and the qcow2 size.
add_executable(lsw_host_domain_description_test domain_description_test.cpp)
target_link_libraries(lsw_host_domain_description_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_domain_description_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_domain_description_test COMMAND lsw_host_domain_description_test)

# The Settings page's disk size, on a real qcow2: grows, never shrinks.
add_executable(lsw_host_disk_grow_test disk_grow_test.cpp)
target_link_libraries(lsw_host_disk_grow_test PRIVATE lsw_host_core LSW::CompileOptions)
target_compile_definitions(lsw_host_disk_grow_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_disk_grow_test COMMAND lsw_host_disk_grow_test)

# The Console page's session picker: list_sessions decoded.
add_executable(lsw_host_guest_sessions_test guest_sessions_test.cpp)
target_link_libraries(lsw_host_guest_sessions_test
    PRIVATE lsw_host_core LSW::CompileOptions nlohmann_json::nlohmann_json)
target_compile_definitions(lsw_host_guest_sessions_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_sessions_test COMMAND lsw_host_guest_sessions_test)

# The redeploy's guest side: the file server's names and the swap
# script's handling of the window mirror's guest half.
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

# The USB devices offered to the guest, read from a sysfs built in a
# temporary directory: what is a device, what is in use, what is HID.
add_executable(lsw_host_usb_devices_test host_usb_devices_test.cpp)
target_link_libraries(lsw_host_usb_devices_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_usb_devices_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_usb_devices_test COMMAND lsw_host_usb_devices_test)

# Where a host file is in Windows, by the share that holds it, and the
# names Windows cannot spell.
add_executable(lsw_host_guest_document_path_test guest_document_path_test.cpp)
target_link_libraries(lsw_host_guest_document_path_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_guest_document_path_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_guest_document_path_test COMMAND lsw_host_guest_document_path_test)

# What publishing a MimeType must never do: take over the host's default
# for a type. The pins in mimeapps.list, and the extension rule.
add_executable(lsw_host_mime_defaults_test mime_defaults_test.cpp)
target_link_libraries(lsw_host_mime_defaults_test
    PRIVATE
        lsw_host_core
        LSW::CompileOptions
)
target_compile_definitions(lsw_host_mime_defaults_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_mime_defaults_test COMMAND lsw_host_mime_defaults_test)

# What a guest update ships and decides, with no guest: the bundle's manifest
# and release from files in a temporary directory, the run's decision on each
# system_info reply, and the batch files' text and exit codes.
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

# The payload ISO's tree: each bundle file in real directories under
# update/, the lists beside them.
add_executable(lsw_host_payload_iso_test payload_iso_test.cpp)
target_link_libraries(lsw_host_payload_iso_test PRIVATE lsw_host_provision LSW::CompileOptions)
target_compile_definitions(lsw_host_payload_iso_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lsw_host_payload_iso_test COMMAND lsw_host_payload_iso_test)

# GuestUpkeep's pass per boot against a fake agent and a stand-in for the
# update; the update dialog's opening, its lack of a way out and its size.
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
