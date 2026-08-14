# lsw-guest-agent: Windows process that lives inside each LSW VM.
# Autostarted from ``HKLM\...\Run\lsw-agent`` (value written by the
# autounattend's Specialize.ps1). The shell processes Run values only
# at a real interactive logon — the post-OOBE AutoLogon of the wizard
# user — so the agent runs AS THAT USER, IN their session. Window
# enumeration/capture (``EnumWindows``/``PrintWindow``/``GetDC``) and
# app launch (plain ``CreateProcessW``) are all native — no SYSTEM,
# no session bridging. The account is a local admin; UAC stays
# enabled (EnableLUA=1) with silent elevation
# (ConsentPromptBehaviorAdmin=0).
#
# Responsibilities:
#   * Report OS / RAM / disk / hostname / IP.
#   * List installed apps (Uninstall registry).
#   * Launch apps via plain ``CreateProcessW`` (we are already the
#     user, in the user's session — no ``CreateProcessAsUser`` dance).
#   * Enumerate / capture / stream the user's windows (native, since
#     the agent runs in the user session).
#   * Push log records back to the manager over the bidirectional
#     RPC channel on TCP 5102.
#
# Build:
#   * Native Windows (MSVC) when LSW_BUILD_AGENT is set.
#   * Cross-build via MinGW-w64 from Linux (for development convenience).

if(NOT WIN32)
    return()
endif()

# .rc compiler (windres on mingw) — needed to embed the UAC manifest
# resource so Windows auto-elevates the agent at launch. Without it
# the agent inherits the wizard user's filtered (Medium-integrity)
# token via HKLM\Run; with it the agent runs High-integrity silently
# (UAC config in the autounattend is ConsentPromptBehaviorAdmin=0 +
# PromptOnSecureDesktop=0, so no prompt is shown).
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
    src/transport/shmem_channel.cpp
)
set(_lsw_agent_rpc_sources
    src/rpc/registry.cpp
    # Per-RPC handler TUs — one per command. Each TU registers itself
    # at static-init via LSW_REGISTER_RPC; the transport dispatcher
    # is now a single registry lookup. Adding a new RPC means a new
    # TU in this list (Phase 4 of the refactor; subsequent UIA RPCs
    # land under src/rpc/handlers/uia/ in Phase 8+).
    src/rpc/handlers/system/system_info.cpp
    src/rpc/handlers/apps/list_apps.cpp
    src/rpc/handlers/apps/launch.cpp
    src/rpc/handlers/apps/get_icon.cpp
    src/rpc/handlers/window/list_windows.cpp
    src/rpc/handlers/window/close_window.cpp
    src/rpc/handlers/window/focus_window.cpp
    src/rpc/handlers/window/unfocus_window.cpp
    src/rpc/handlers/window/resize_window.cpp
    src/rpc/handlers/window/window_props.cpp
    src/rpc/handlers/window/nchittest.cpp
    src/rpc/handlers/window/get_window_icon.cpp
    src/rpc/handlers/input/inject_input.cpp
    src/rpc/handlers/capture/subscribe_window.cpp
    src/rpc/handlers/capture/unsubscribe_window.cpp
    src/rpc/handlers/capture/capture_debug.cpp
    src/rpc/handlers/capture/set_capture_method.cpp
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
    src/rpc/handlers/uia/uia_unsubscribe_lifecycle.cpp
    src/rpc/handlers/uia/uia_subscribe_focus.cpp
    src/rpc/handlers/uia/uia_subscribe_properties.cpp
    src/rpc/handlers/uia/uia_unsubscribe.cpp
)
set(_lsw_agent_win_sources
    src/win/window.cpp
    src/win/input.cpp
    src/win/icon_extractor.cpp
    src/win/gdiplus_runtime.cpp
    src/win/com.cpp
    src/win/move_resize_watcher.cpp
    src/win/desktop_session.cpp
    src/win/desktop_probe.cpp
    src/win/token_privileges.cpp
)
set(_lsw_agent_util_sources
    src/util/base64.cpp
    src/util/text_encoding.cpp
)
set(_lsw_agent_capture_sources
    src/capture/wgc.cpp
    src/capture/setup.cpp
    src/capture/vulkan_session.cpp
)
set(_lsw_agent_uia_sources
    src/uia/uia.cpp
    src/uia/uia_root.cpp
    src/uia/uia_event_queue.cpp
    src/uia/bstr_text.cpp
    src/uia/element_locator.cpp
    src/uia/caption_button_query.cpp
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
    src/apps/launcher.cpp
    src/apps/d3d_translation.cpp
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
)

add_executable(lsw-guest-agent
    ${_lsw_agent_core_sources}
    ${_lsw_agent_transport_sources}
    ${_lsw_agent_rpc_sources}
    ${_lsw_agent_util_sources}
    ${_lsw_agent_win_sources}
    ${_lsw_agent_capture_sources}
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
        # IvshmemRegion: the one implementation of "map the BAR", shared
        # with the guest ICD. Brings in no logging — lsw_protocol has no
        # spdlog dependency, which is what lets the agent keep its own
        # logger without a second one arriving through the back door.
        LSW::Protocol
        nlohmann_json::nlohmann_json
        ws2_32        # WinSock2
        iphlpapi      # GetAdaptersAddresses
        gdi32         # GDI window capture (PrintWindow/BitBlt)
        gdiplus       # PNG encoding
        advapi32      # registry / SetConsoleCtrlHandler
        shlwapi       # PathMatchSpec etc.
        winhttp       # Installer: HTTP(S) downloads
        dwmapi        # DWMWA_CLOAKED / EXTENDED_FRAME_BOUNDS
        runtimeobject # WinRT (RoInitialize / RoGetActivationFactory)
        d3d11         # WGC capture device
        dxgi          # DXGI device interop for WGC
        setupapi      # SetupDi* — locate the IVSHMEM PCI device
        cfgmgr32      # CM_* helpers used by setupapi
        ole32         # COM (CoInitializeEx / CoCreateInstance for UIA)
        oleaut32      # COM BSTR/VARIANT used by UIA
        uuid          # CLSID_CUIAutomation, IID_IUIAutomation*
)

target_compile_definitions(lsw-guest-agent PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# main() is wmain() — wide-char entry point. Mingw-w64 needs
# ``-municode`` at link time to swap in the wmain-aware startup
# object, otherwise the linker complains about an undefined
# ``WinMain``/``main``. Applied per-target so it doesn't interfere
# with CMake's compiler probe (which uses plain main).
if(MINGW)
    target_link_options(lsw-guest-agent PRIVATE -municode)
endif()
