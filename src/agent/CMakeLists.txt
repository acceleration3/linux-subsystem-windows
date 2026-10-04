# lsw-guest-agent: the Windows program inside each LSW VM, deployed as
# C:\ProgramData\LSW\lsw-agent.exe. One executable, three roles
# (src/main.cpp):
#
#   * ``--service`` — the ``lsw-agent`` Windows service, LocalSystem, in
#     session 0, registered by the autounattend's LswPromote.ps1 at
#     SetupComplete. Owns the manager link (TCP 5102 + the IVSHMEM ring),
#     RPC dispatch, installs, consoles, and process launching into any
#     session as that session's user (WTSQueryUserToken +
#     CreateProcessAsUserW, the user's full token by default).
#   * ``--session --pipe NAME`` — a session helper the service keeps in
#     each user session, for what only works from inside one: window
#     enumeration and commands, UI Automation, HKCU, and the desktop gate
#     that holds the service's listener closed until the desktop is
#     usable. Talks to the service over a named pipe in the link's own
#     envelope framing.
#   * no arguments — the service's work in the foreground, for debugging.
#
# A GUI-subsystem program (WIN32_EXECUTABLE): nothing it is or starts for
# itself ever shows a console window. It logs to C:\ProgramData\LSW.
#
# Responsibilities:
#   * Report OS / RAM / disk / hostname / IP.
#   * List installed apps (Uninstall registry).
#   * Launch apps into a user session, and start the window mirror's
#     guest half beside them. The windows themselves — their pixels and
#     their input — belong to that mirror and travel on its own
#     connection, not this one.
#   * Push log records back to the manager over the bidirectional RPC
#     channel on TCP 5102.
#
# Build:
#   * Native Windows (MSVC) when LSW_BUILD_AGENT is set.
#   * Cross-build via MinGW-w64 from Linux (for development convenience).

if(NOT WIN32)
    return()
endif()

# .rc compiler (windres on mingw) — embeds the application manifest
# (res/lsw-agent.manifest: asInvoker, so the service can start a session
# helper as a user without a UAC prompt).
enable_language(RC)

# Sources are grouped by module — each subdirectory is one feature
# area. Adding a new RPC handler in Phase 4+ means a new TU under
# src/rpc/handlers/<category>/<name>.cpp; that TU gets listed here
# (no GLOB — explicit listing is more reliable, especially with the
# MinGW + Ninja cross-build).
set(_lsw_agent_core_sources
    src/main.cpp
    src/log.cpp
    src/core/agent_service.cpp
    src/core/command_line.cpp
    src/core/paths.cpp
    src/core/service_host.cpp
    src/core/services.cpp
    res/lsw-agent.rc
)
# The session layer: which sessions exist, the helper in each, and the
# pipe between it and the service. The first four TUs are platform-free
# and also built into the host-native tests (tests/CMakeLists.txt).
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
    # One TU per push-event source. Each adapts a service's own typed
    # notifications into rpc::Event, so the connection loop drains them
    # all through the same EventPump.
    src/transport/log_event_source.cpp
    src/transport/console_event_source.cpp
    src/transport/installer_event_source.cpp
    src/transport/shmem.cpp
    src/transport/ivshmem_region.cpp
    src/transport/shmem_channel.cpp
)
set(_lsw_agent_rpc_sources
    src/rpc/registry.cpp
    src/rpc/params.cpp
    # Per-RPC handler TUs — one per command. Each TU registers itself
    # at static-init via LSW_REGISTER_RPC; the transport dispatcher
    # is now a single registry lookup. Adding a new RPC means a new
    # TU in this list (Phase 4 of the refactor; subsequent UIA RPCs
    # land under src/rpc/handlers/uia/ in Phase 8+).
    src/rpc/handlers/system/system_info.cpp
    src/rpc/handlers/system/set_display.cpp
    src/rpc/handlers/system/gpu_status.cpp
    src/rpc/handlers/system/list_sessions.cpp
    src/rpc/handlers/apps/list_apps.cpp
    src/rpc/handlers/apps/launch.cpp
    src/rpc/handlers/apps/get_icon.cpp
    src/rpc/handlers/apps/list_dir.cpp
    src/rpc/handlers/apps/resolve_shortcut.cpp
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
    src/win/desktop_probe.cpp
    src/win/token_privileges.cpp
    src/win/error_text.cpp
    src/win/bstr.cpp
    src/win/reg_key.cpp
    src/win/environment.cpp
    src/win/file_system.cpp
    src/win/process_info.cpp
    src/win/shell_link.cpp
    src/win/process.cpp
    src/win/run_as.cpp
    src/win/wts_sessions.cpp
    src/win/security_attributes.cpp
    src/win/overlapped_pipe.cpp
)
set(_lsw_agent_util_sources
    src/util/base64.cpp
    src/util/text_encoding.cpp
    src/util/wide_string.cpp
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
    src/apps/catalog.cpp
    src/apps/launch_group.cpp
    src/apps/launcher.cpp
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
)

# Build stamp: regenerated EVERY build so agent_version always
# changes (redeploy_agent polls system_info until it does). The
# custom target has no OUTPUT, only BYPRODUCTS, so it always runs;
# OBJECT_DEPENDS forces system_info.cpp to recompile when the (always
# freshly-written) stamp changes.
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
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(lsw-guest-agent
    PRIVATE
        LSW::CompileOptions
        LSW::LinkProto   # wire contract shared with the manager
        nlohmann_json::nlohmann_json
        ws2_32        # WinSock2
        iphlpapi      # GetAdaptersAddresses
        gdi32         # icon bitmaps
        gdiplus       # PNG encoding
        advapi32      # registry, tokens, the service control manager
        shlwapi       # PathMatchSpec etc.
        winhttp       # Installer: HTTP(S) downloads
        dwmapi        # DWMWA_CLOAKED / EXTENDED_FRAME_BOUNDS
        runtimeobject # WinRT (RoInitialize / RoGetActivationFactory)
        setupapi      # SetupDi* — locate the IVSHMEM PCI device
        cfgmgr32      # CM_* helpers used by setupapi
        ole32         # COM (CoInitializeEx / CoCreateInstance for UIA)
        oleaut32      # COM BSTR/VARIANT used by UIA
        uuid          # CLSID_CUIAutomation, IID_IUIAutomation*
        wtsapi32      # WTSEnumerateSessions / WTSQueryUserToken
        userenv       # CreateEnvironmentBlock / ExpandEnvironmentStringsForUser
)

target_compile_definitions(lsw-guest-agent PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# GUI subsystem: no console window for the service, a helper or a
# debugging run. The entry point stays wmain().
set_target_properties(lsw-guest-agent PROPERTIES WIN32_EXECUTABLE TRUE)

# main() is wmain() — wide-char entry point. Mingw-w64 needs
# ``-municode`` at link time to swap in the wmain-aware startup
# object (which it uses for the GUI subsystem too), otherwise the
# linker complains about an undefined ``WinMain``/``main``. MSVC is
# told the same with its entry point. Applied per-target so it doesn't
# interfere with CMake's compiler probe (which uses plain main).
#
# The mingw toolchain files put ``-mconsole`` in every executable's link
# flags (cmake/toolchain-mingw-w64.cmake), which beats WIN32_EXECUTABLE's
# ``-mwindows``; the linker's own option, given last, settles it.
if(MINGW)
    target_link_options(lsw-guest-agent PRIVATE -municode -Wl,--subsystem,windows)
elseif(MSVC)
    target_link_options(lsw-guest-agent PRIVATE /ENTRY:wmainCRTStartup)
endif()

# A file the payload ISO carries, not a program for this machine: under
# share/, by its build name (the stager names it lsw-agent.exe on the
# ISO).
install(TARGETS lsw-guest-agent
        RUNTIME DESTINATION ${LSW_INSTALL_GUESTDIR}
        COMPONENT ${LSW_INSTALL_COMPONENT})
