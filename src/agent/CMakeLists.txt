if(NOT WIN32)
    return()
endif()

enable_language(RC)

set(_lsw_agent_core_sources
    src/main.cpp
    src/log.cpp
    src/core/agent_service.cpp
    src/core/command_line.cpp
    src/core/paths.cpp
    src/core/startup_header.cpp
    src/core/service_host.cpp
    src/core/services.cpp
    res/lsw-agent.rc
)
set(_lsw_agent_session_sources
    src/session/session_selector.cpp
    src/session/session_routing.cpp
    src/session/pipe_codec.cpp
    src/session/pending_calls.cpp
    src/session/helper_channel.cpp
    src/session/session_manager.cpp
    src/session/session_host.cpp
)
set(_lsw_agent_transport_sources
    src/transport/host_endpoint.cpp
    src/transport/envelope_writer.cpp
    src/transport/event_pump.cpp
    src/transport/log_event_source.cpp
    src/transport/console_event_source.cpp
    src/transport/installer_event_source.cpp
)
set(_lsw_agent_rpc_sources
    src/rpc/registry.cpp
    src/rpc/params.cpp
    src/rpc/handlers/system/system_info.cpp
    src/rpc/handlers/system/set_display.cpp
    src/rpc/handlers/system/gpu_status.cpp
    src/rpc/handlers/system/list_sessions.cpp
    src/rpc/handlers/system/file_hashes.cpp
    src/rpc/handlers/system/set_clock.cpp
    src/rpc/handlers/system/set_log_level.cpp
    src/rpc/handlers/system/read_file.cpp
    src/rpc/handlers/system/pack_logs.cpp
    src/rpc/handlers/apps/list_apps.cpp
    src/rpc/handlers/apps/launch.cpp
    src/rpc/handlers/apps/get_icon.cpp
    src/rpc/handlers/apps/list_dir.cpp
    src/rpc/handlers/apps/resolve_shortcut.cpp
    src/rpc/handlers/apps/upload_file.cpp
    src/rpc/handlers/apps/open_package.cpp
    src/rpc/handlers/apps/list_store_apps.cpp
    src/rpc/handlers/apps/launch_store_app.cpp
    src/rpc/handlers/apps/file_handler.cpp
    src/rpc/handlers/window/start_window_mirror.cpp
    src/rpc/handlers/window/list_windows.cpp
    src/rpc/handlers/window/close_window.cpp
    src/rpc/handlers/window/window_command.cpp
    src/rpc/handlers/install/download_install.cpp
    src/rpc/handlers/install/download_extract.cpp
    src/rpc/handlers/install/run_script.cpp
    src/rpc/handlers/console/console_open.cpp
    src/rpc/handlers/console/console_input.cpp
    src/rpc/handlers/console/console_close.cpp
    src/rpc/handlers/uia/uia_state.cpp
    src/rpc/handlers/uia/uia_invoke.cpp
    src/rpc/handlers/uia/uia_set_value.cpp
    src/rpc/handlers/uia/uia_get_text.cpp
    src/rpc/handlers/uia/uia_tree_dump.cpp
    src/rpc/handlers/uia/uia_subscribe_lifecycle.cpp
    src/rpc/handlers/uia/uia_subscribe_focus.cpp
    src/rpc/handlers/uia/uia_subscribe_properties.cpp
    src/rpc/handlers/uia/uia_unsubscribe.cpp
)
set(_lsw_agent_win_sources
    src/win/mirror_process.cpp
    src/win/window.cpp
    src/win/icon_extractor.cpp
    src/win/gdiplus_runtime.cpp
    src/win/com.cpp
    src/win/desktop_session.cpp
    src/win/display_scale.cpp
    src/win/token_privileges.cpp
    src/win/error_text.cpp
    src/win/bstr.cpp
    src/win/reg_key.cpp
    src/win/environment.cpp
    src/win/file_system.cpp
    src/win/file_digest.cpp
    src/win/process_info.cpp
    src/win/shell_link.cpp
    src/win/process.cpp
    src/win/run_as.cpp
    src/win/wts_sessions.cpp
    src/win/security_attributes.cpp
    src/win/overlapped_pipe.cpp
    src/win/rolling_file.cpp
)
set(_lsw_agent_util_sources
    src/util/base64.cpp
    src/util/text_encoding.cpp
    src/util/wide_string.cpp
    src/util/powershell_command.cpp
)
set(_lsw_agent_uia_sources
    src/uia/uia.cpp
    src/uia/uia_window.cpp
    src/uia/uia_root.cpp
    src/uia/uia_event_queue.cpp
    src/uia/element_locator.cpp
    src/uia/window_state_query.cpp
    src/uia/element_invoker.cpp
    src/uia/element_value_setter.cpp
    src/uia/text_extractor.cpp
    src/uia/tree_dumper.cpp
    src/uia/lifecycle_event_handler.cpp
    src/uia/focus_event_handler.cpp
    src/uia/property_event_handler.cpp
    src/uia/subscription_registry.cpp
)
set(_lsw_agent_apps_sources
    src/apps/app_installer_window.cpp
    src/apps/store_apps.cpp
    src/apps/catalog.cpp
    src/apps/program_command.cpp
    src/apps/apps_folder.cpp
    src/apps/start_menu_watcher.cpp
    src/apps/start_menu_filter.cpp
    src/apps/file_associations.cpp
    src/apps/launch_group.cpp
    src/apps/launch_command.cpp
    src/apps/launcher.cpp
    src/apps/upload_store.cpp
)
set(_lsw_agent_install_sources
    src/install/installer.cpp
    src/install/install_job.cpp
    src/install/download_install_job.cpp
    src/install/download_extract_job.cpp
    src/install/run_script_job.cpp
    src/install/http_downloader.cpp
    src/install/process_runner.cpp
    src/install/notification_queue.cpp
    src/install/job_id_generator.cpp
    src/install/directory_maker.cpp
    src/install/script_file.cpp
    src/install/work_dir.cpp
    src/install/url_file_name.cpp
)
set(_lsw_agent_console_sources
    src/console/console.cpp
)
set(_lsw_agent_system_sources
    src/system/info.cpp
    src/system/volume_grower.cpp
    src/system/scale_steps.cpp
    src/system/log_packer.cpp
    src/system/driver_log_pruner.cpp
)

add_executable(lsw-guest-agent
    ${_lsw_agent_core_sources}
    ${_lsw_agent_session_sources}
    ${_lsw_agent_transport_sources}
    ${_lsw_agent_rpc_sources}
    ${_lsw_agent_util_sources}
    ${_lsw_agent_win_sources}
    ${_lsw_agent_uia_sources}
    ${_lsw_agent_apps_sources}
    ${_lsw_agent_install_sources}
    ${_lsw_agent_console_sources}
    ${_lsw_agent_system_sources}
    ${CMAKE_SOURCE_DIR}/src/base/src/repeat_collapser.cpp
)

set(_lsw_build_stamp "${CMAKE_CURRENT_BINARY_DIR}/lsw_build_stamp.h")
add_custom_target(lsw_agent_stamp
    BYPRODUCTS "${_lsw_build_stamp}"
    COMMAND ${CMAKE_COMMAND} -DOUT=${_lsw_build_stamp}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen-build-stamp.cmake
    COMMENT "Generating lsw-guest-agent build stamp"
    VERBATIM
)
add_dependencies(lsw-guest-agent lsw_agent_stamp)
set_source_files_properties(src/system/info.cpp
    PROPERTIES OBJECT_DEPENDS "${_lsw_build_stamp}")

target_include_directories(lsw-guest-agent
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/base/include
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(lsw-guest-agent
    PRIVATE
        LSW::CompileOptions
        LSW::LinkProto
        nlohmann_json::nlohmann_json
        ws2_32
        iphlpapi
        gdi32
        gdiplus
        advapi32
        shlwapi
        shell32
        winhttp
        dwmapi
        runtimeobject
        setupapi
        cfgmgr32
        bcrypt
        ole32
        oleaut32
        uuid
        wtsapi32
        userenv
)

target_compile_definitions(lsw-guest-agent PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set_target_properties(lsw-guest-agent PROPERTIES WIN32_EXECUTABLE TRUE)

if(MINGW)
    target_link_options(lsw-guest-agent PRIVATE -municode -Wl,--subsystem,windows)
elseif(MSVC)
    target_link_options(lsw-guest-agent PRIVATE /ENTRY:wmainCRTStartup)
endif()

install(TARGETS lsw-guest-agent
        RUNTIME DESTINATION ${LSW_INSTALL_GUESTDIR}
        COMPONENT ${LSW_INSTALL_COMPONENT})
