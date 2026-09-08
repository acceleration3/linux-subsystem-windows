# Host-side (Linux) code. Four libraries plus the executable.
#
# One include/ tree and one src/ tree, subdivided by concern:
#
#   include/lsw/host/<concern>/<class>.hpp
#   src/<concern>/<class>.cpp
#
# The concern subdivision is finer-grained than the target list on
# purpose: subdirectories are how the code is read, targets are how the
# build is linked. Every include in the repo reads
# "lsw/host/<concern>/<class>.hpp" regardless of which library the class
# ends up in, so moving a concern between targets touches no source.
#
#   lsw_host_core       platform  OS plumbing: DBus single-instance, vsock, XDG
#                       config    persisted settings + the VM manifest
#                       vm        libvirt lifecycle, stats, shared-memory transport
#                       link      the multiplexed JSON channel to the guest agent
#                       apps      guest app catalog, per-app config, launch, logs
#                       mirror    the window mirror a launch starts, one per
#                                 application
#                       ui        widgets shared by both UIs (VncView)
#                       services  HostServices — the container everything takes
#
#   lsw_host_provision  provision first-run install pipeline (ISO, autounattend,
#                                 setup steps). No widgets; two consumers, the
#                                 wizard UI and the --unattended CLI.
#
#   lsw_host_wizard     wizard    the first-run QWizard and its pages
#   lsw_host_manager    manager   the everyday window
#                       mcp       external automation surface (MCP over HTTP)
#
#   lsw-manager         app       composition root + entry point
#
# The wizard and the manager are separate targets so neither can include
# the other's headers: they are different applications that happen to
# ship in one binary.

set(CMAKE_AUTOMOC ON)

# Collect the sources for a list of concerns. Headers are listed as
# sources so AUTOMOC scans the Q_OBJECT ones; CONFIGURE_DEPENDS re-globs
# on every build, so adding a class needs no CMake edit.
function(lsw_host_sources out)
    set(_all "")
    foreach(concern ${ARGN})
        file(GLOB_RECURSE _s CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/src/${concern}/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/lsw/host/${concern}/*.hpp)
        list(APPEND _all ${_s})
    endforeach()
    set(${out} ${_all} PARENT_SCOPE)
endfunction()

# The single include root every host target exports. Which concerns a
# target actually contains is decided by its source list above; the
# include path is uniform so no source ever names a target.
set(LSW_HOST_INCLUDE_DIR $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# By module path, not by absolute path: CMAKE_MODULE_PATH is where the
# root says to find it.
include(EmbedTextFile)

# ---- lsw_host_core -------------------------------------------------------
set(LSW_CORE_CONCERNS platform config vm link apps mirror ui services)

# The libvirt domain XML template (vm/domain_xml_builder.cpp) is embedded
# at configure time so the C++ stays free of a giant raw-string literal.
set(_core_embed "${CMAKE_CURRENT_BINARY_DIR}/embedded/core")
file(MAKE_DIRECTORY "${_core_embed}")
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/domain.xml"
                "${_core_embed}/domain_template.hpp" kDomainTemplate)
# The stylesheet both applications wear (ui/theme.cpp). Embedded for the
# same reason: a theme read from disk is one more thing to install, and
# a manager that cannot find it looks broken rather than plain.
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/theme.qss"
                "${_core_embed}/theme_qss.hpp" kThemeQss)

lsw_host_sources(LSW_CORE_SOURCES ${LSW_CORE_CONCERNS})
add_library(lsw_host_core STATIC ${LSW_CORE_SOURCES})
target_include_directories(lsw_host_core PUBLIC ${LSW_HOST_INCLUDE_DIR})
# vm/render_stats.cpp reads the render server's region header. Its
# layout is the contract between the two, so the header itself is
# included rather than mirrored: a mirror is a copy that drifts, and it
# drifts silently because both sides still compile.
set(_rvk_wire_include
    "${CMAKE_SOURCE_DIR}/external/render-server/src/wire/include")
target_include_directories(lsw_host_core
    PRIVATE $<BUILD_INTERFACE:${_core_embed}> $<BUILD_INTERFACE:${_rvk_wire_include}>)
target_link_libraries(lsw_host_core
    PUBLIC
        Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Xml Qt6::Concurrent
        PkgConfig::LIBVIRT      # virConnectPtr / virDomainPtr are in the headers
        LSW::Base
        LSW::LinkProto          # wire contract shared with the guest agent
    PRIVATE
        LSW::CompileOptions
        nlohmann_json::nlohmann_json
        PkgConfig::LIBVNCCLIENT  # ui/vnc_view.cpp
)
target_compile_definitions(lsw_host_core PRIVATE QT_NO_KEYWORDS)

# ---- lsw_host_provision --------------------------------------------------
set(_prov_embed "${CMAKE_CURRENT_BINARY_DIR}/embedded/provision")
file(MAKE_DIRECTORY "${_prov_embed}")
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/autounattend.xml"
                "${_prov_embed}/autounattend_template.hpp" kAutounattendTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/win11-bypass.xml"
                "${_prov_embed}/win11_bypass_template.hpp" kWin11BypassTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/setup-steps.json"
                "${_prov_embed}/setup_steps_template.hpp" kSetupStepsTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/scripts/tune-windows.cmd"
                "${_prov_embed}/scripts/tune_windows.hpp" kScriptTuneWindows)

lsw_host_sources(LSW_PROVISION_SOURCES provision)
add_library(lsw_host_provision STATIC ${LSW_PROVISION_SOURCES})
target_include_directories(lsw_host_provision PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_include_directories(lsw_host_provision
    PRIVATE $<BUILD_INTERFACE:${_prov_embed}> ${WIMLIB_INCLUDE_DIR} ${HIVEX_INCLUDE_DIR})
target_link_libraries(lsw_host_provision
    PUBLIC
        lsw_host_core
    PRIVATE
        LSW::CompileOptions
        nlohmann_json::nlohmann_json
        ZLIB::ZLIB              # iso/microsoft_client.cpp gunzip fallback
        PkgConfig::LIBISOFS     # install/payload_iso.cpp
        ${WIMLIB_LIBRARY}       # install/wim_editions.cpp, debloat/wim_debloater.cpp
        ${HIVEX_LIBRARY}        # debloat/registry_tweaks.cpp
)
target_compile_definitions(lsw_host_provision PRIVATE QT_NO_KEYWORDS)

# ---- lsw_host_tools ------------------------------------------------------
# The image tools: download one, trim one. They used to be wizard pages,
# which meant they could only run once, in one place, on the way to
# something else. Both the wizard and the manager open them now, so they
# live in a target of their own that neither owns.
lsw_host_sources(LSW_TOOLS_SOURCES tools)
add_library(lsw_host_tools STATIC ${LSW_TOOLS_SOURCES})
target_include_directories(lsw_host_tools PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw_host_tools
    PUBLIC  lsw_host_provision
    PRIVATE LSW::CompileOptions
)
target_compile_definitions(lsw_host_tools PRIVATE QT_NO_KEYWORDS)

# ---- lsw_host_wizard -----------------------------------------------------
lsw_host_sources(LSW_WIZARD_SOURCES wizard)
add_library(lsw_host_wizard STATIC ${LSW_WIZARD_SOURCES})
target_include_directories(lsw_host_wizard PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw_host_wizard
    PUBLIC  lsw_host_provision lsw_host_tools
    PRIVATE LSW::CompileOptions nlohmann_json::nlohmann_json
)
target_compile_definitions(lsw_host_wizard PRIVATE QT_NO_KEYWORDS)

# ---- lsw_host_manager ----------------------------------------------------
lsw_host_sources(LSW_MANAGER_SOURCES manager mcp)
add_library(lsw_host_manager STATIC ${LSW_MANAGER_SOURCES})
target_include_directories(lsw_host_manager PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw_host_manager
    PUBLIC  lsw_host_core lsw_host_tools
    PRIVATE LSW::CompileOptions nlohmann_json::nlohmann_json
)
target_compile_definitions(lsw_host_manager PRIVATE QT_NO_KEYWORDS)

# window_view.cpp stamps a per-window WM_CLASS via xcb on X11.
if(TARGET PkgConfig::XCB)
    target_link_libraries(lsw_host_manager PRIVATE PkgConfig::XCB)
    target_compile_definitions(lsw_host_manager PRIVATE LSW_HAVE_XCB=1)
endif()

# ---- lsw-manager ---------------------------------------------------------
# The only translation units that know about BOTH UIs.
lsw_host_sources(LSW_APP_SOURCES app)
add_executable(lsw-manager ${LSW_APP_SOURCES})
target_include_directories(lsw-manager PRIVATE ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw-manager
    PRIVATE LSW::CompileOptions lsw_host_wizard lsw_host_manager
)
target_compile_definitions(lsw-manager PRIVATE QT_NO_KEYWORDS)

if(LSW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
