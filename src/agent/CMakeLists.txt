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

add_executable(lsw-guest-agent
    src/main.cpp
    src/agent_service.cpp
    src/host_endpoint.cpp
    src/installer.cpp
    src/log.cpp
    src/app_catalog.cpp
    src/capture_setup.cpp
    src/console.cpp
    src/gdi_util.cpp
    src/input_inject.cpp
    src/process_launcher.cpp
    src/system_info.cpp
    src/window_capture.cpp
    src/wgc_capture.cpp
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
set_source_files_properties(src/system_info.cpp
    PROPERTIES OBJECT_DEPENDS "${_lsw_build_stamp}")

target_include_directories(lsw-guest-agent
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(lsw-guest-agent
    PRIVATE
        LSW::CompileOptions
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
