# lsw-manager: Qt 6 Widgets host UI.
#
# Two phases of UX:
#   * First-run wizard — ISO acquisition, VM provisioning, unattended
#     install, payload install. ``ui/wizard/*``.
#   * Manager UI — VM list, live stats, app launcher. ``ui/manager/*``.
#
# Internal layering (all OOP, one class per file pair):
#   config/    settings + persisted VM manifest (vms.json)
#   vm/        libvirt connection, domain XML builder, lifecycle, stats monitor, vsock
#   iso/       Windows ISO catalog, async downloader, hash verifier
#   install/   autounattend.xml generator, payload ISO builder, post-install watcher
#   apps/      Start-menu app catalog (via guest agent), launcher
#   ui/        Qt widgets: wizard pages, main window, VM panels
#
# libvirt is required (every modern Linux distro packages it); pkg-config
# drives the find. Qt 6 Widgets + Network + DBus + Concurrent.

if(NOT (UNIX AND NOT APPLE))
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Concurrent)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)  # MicrosoftClient: defensive gunzip fallback
pkg_check_modules(LIBVIRT REQUIRED IMPORTED_TARGET libvirt)
pkg_check_modules(LIBISOFS REQUIRED IMPORTED_TARGET libisofs-1)  # PayloadIsoBuilder
pkg_check_modules(LIBVNCCLIENT REQUIRED IMPORTED_TARGET libvncclient)  # VncView

# libwim (wimlib) — list Windows editions in the install ISO. Ships no
# pkg-config / CMake config, so resolve the lib + header by hand.
find_library(WIMLIB_LIBRARY NAMES wim REQUIRED)
find_path(WIMLIB_INCLUDE_DIR NAMES wimlib.h REQUIRED)

# Embed the Windows-side templates and scripts (autounattend.xml +
# win11-bypass.xml fragment + setup-steps.json + the tune-* scripts
# under embedded/scripts/) at configure time so the C++ source stays
# free of giant raw-string literals and the files can be edited as
# their own .xml / .cmd / .json under src/manager/embedded/.
#
# The CMake-generated header tree lands under
# ${CMAKE_CURRENT_BINARY_DIR}/embedded (same name, different physical
# path) — it's the build-tree mirror of the source-tree directory.
include(${CMAKE_SOURCE_DIR}/cmake/EmbedTextFile.cmake)
set(_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${_embed_dir}")
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/autounattend.xml"
                "${_embed_dir}/autounattend_template.hpp"
                kAutounattendTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/win11-bypass.xml"
                "${_embed_dir}/win11_bypass_template.hpp"
                kWin11BypassTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/domain.xml"
                "${_embed_dir}/domain_template.hpp"
                kDomainTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/setup-steps.json"
                "${_embed_dir}/setup_steps_template.hpp"
                kSetupStepsTemplate)

# Scripts referenced from setup-steps.json via ``"script_ref": "..."``.
# Each script lives in its own .cmd so authors edit a real file with
# syntax highlighting and no JSON-escape pain. The runtime resolves
# the ref → embedded constant via SetupPage::resolve_script_ref.
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/scripts/tune-windows.cmd"
                "${_embed_dir}/scripts/tune_windows.hpp"
                kScriptTuneWindows)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_library(lsw_manager STATIC
    # Headers carrying Q_OBJECT — listed so AUTOMOC scans them. They
    # don't get compiled (CMake skips headers); listing them is just
    # how AUTOMOC learns to moc them.
    include/lsw/manager/app.hpp
    include/lsw/manager/single_instance.hpp
    include/lsw/manager/config/settings.hpp
    include/lsw/manager/config/vm_state.hpp
    include/lsw/manager/vm/agent_rpc_client.hpp
    include/lsw/manager/vm/libvirt_conn.hpp
    include/lsw/manager/vm/lifecycle.hpp
    include/lsw/manager/vm/monitor.hpp
    include/lsw/manager/vm/vsock.hpp
    include/lsw/manager/iso/downloader.hpp
    include/lsw/manager/iso/microsoft_client.hpp
    include/lsw/manager/iso/source_fetcher.hpp
    include/lsw/manager/install/post_install.hpp
    include/lsw/manager/apps/app_config.hpp
    include/lsw/manager/apps/app_config_store.hpp
    include/lsw/manager/apps/catalog.hpp
    include/lsw/manager/apps/launcher.hpp
    include/lsw/manager/apps/log_store.hpp
    include/lsw/manager/mcp/mcp_server.hpp
    include/lsw/manager/ui/manager/app_config_dialog.hpp
    include/lsw/manager/ui/manager/app_window_watcher.hpp
    include/lsw/manager/ui/manager/console_window.hpp
    include/lsw/manager/ui/manager/log_viewer.hpp
    include/lsw/manager/ui/manager/window_view.hpp
    include/lsw/manager/ui/vnc_view.hpp
    include/lsw/manager/ui/manager/main_window.hpp
    include/lsw/manager/ui/manager/vm_detail_panel.hpp
    include/lsw/manager/ui/manager/stats_widget.hpp
    include/lsw/manager/ui/manager/app_list.hpp
    include/lsw/manager/ui/wizard/wizard.hpp
    include/lsw/manager/ui/wizard/welcome_page.hpp
    include/lsw/manager/ui/wizard/license_page.hpp
    include/lsw/manager/ui/wizard/iso_choice_page.hpp
    include/lsw/manager/ui/wizard/download_page.hpp
    include/lsw/manager/ui/wizard/vm_size_page.hpp
    include/lsw/manager/ui/wizard/payload_page.hpp
    include/lsw/manager/ui/wizard/install_page.hpp
    include/lsw/manager/ui/wizard/setup_page.hpp
    include/lsw/manager/ui/wizard/done_page.hpp
    include/lsw/manager/cli.hpp
    include/lsw/manager/wizard/microsoft_resolver.hpp
    include/lsw/manager/wizard/pipeline.hpp
    include/lsw/manager/wizard/setup_plan.hpp

    src/app.cpp
    src/cli.cpp
    src/single_instance.cpp

    src/wizard/microsoft_resolver.cpp
    src/wizard/pipeline.cpp
    src/wizard/setup_plan.cpp

    src/config/settings.cpp
    src/config/vm_state.cpp

    src/vm/agent_rpc_client.cpp
    src/vm/libvirt_conn.cpp
    src/vm/domain_xml.cpp
    src/vm/lifecycle.cpp
    src/vm/monitor.cpp
    src/vm/vsock.cpp

    src/iso/catalog.cpp
    src/iso/downloader.cpp
    src/iso/microsoft_client.cpp
    src/iso/source_fetcher.cpp
    src/iso/verifier.cpp

    src/install/autounattend.cpp
    src/install/payload_iso.cpp
    src/install/post_install.cpp
    src/install/wim_editions.cpp

    src/apps/app_config.cpp
    src/apps/app_config_store.cpp
    src/apps/catalog.cpp
    src/apps/launcher.cpp
    src/apps/log_store.cpp

    src/mcp/mcp_server.cpp

    src/ui/manager/app_config_dialog.cpp
    src/ui/manager/app_window_watcher.cpp
    src/ui/manager/console_window.cpp
    src/ui/manager/log_viewer.cpp
    src/ui/manager/window_view.cpp

    src/ui/vnc_view.cpp

    src/ui/manager/main_window.cpp
    src/ui/manager/vm_detail_panel.cpp
    src/ui/manager/stats_widget.cpp
    src/ui/manager/app_list.cpp

    src/ui/wizard/wizard.cpp
    src/ui/wizard/welcome_page.cpp
    src/ui/wizard/license_page.cpp
    src/ui/wizard/iso_choice_page.cpp
    src/ui/wizard/download_page.cpp
    src/ui/wizard/vm_size_page.cpp
    src/ui/wizard/payload_page.cpp
    src/ui/wizard/install_page.cpp
    src/ui/wizard/setup_page.cpp
    src/ui/wizard/done_page.cpp
)
add_library(LSW::Manager ALIAS lsw_manager)

target_include_directories(lsw_manager
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${_embed_dir}>
)

target_link_libraries(lsw_manager
    PUBLIC
        Qt6::Widgets
        Qt6::Network
        Qt6::DBus
        Qt6::Concurrent
        PkgConfig::LIBVIRT
        LSW::Common
        LSW::Protocol
    PRIVATE
        LSW::CompileOptions
        nlohmann_json::nlohmann_json
        ZLIB::ZLIB
        PkgConfig::LIBISOFS
        PkgConfig::LIBVNCCLIENT
        ${WIMLIB_LIBRARY}
)

target_include_directories(lsw_manager PRIVATE ${WIMLIB_INCLUDE_DIR})

target_compile_definitions(lsw_manager PRIVATE
    QT_NO_KEYWORDS  # use Q_SIGNALS / Q_SLOTS / Q_EMIT to avoid clashes
)

add_executable(lsw-manager src/main.cpp)
target_link_libraries(lsw-manager PRIVATE LSW::Manager LSW::CompileOptions)

if(LSW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
