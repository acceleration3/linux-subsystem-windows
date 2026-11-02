set(CMAKE_AUTOMOC ON)

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

set(LSW_HOST_INCLUDE_DIR $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

include(EmbedTextFile)

set(LSW_CORE_CONCERNS platform config vm link apps mirror ui services)

set(_core_embed "${CMAKE_CURRENT_BINARY_DIR}/embedded/core")
file(MAKE_DIRECTORY "${_core_embed}")
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/domain.xml"
                "${_core_embed}/domain_template.hpp" kDomainTemplate)
embed_text_file("${CMAKE_CURRENT_SOURCE_DIR}/embedded/theme.qss"
                "${_core_embed}/theme_qss.hpp" kThemeQss)

lsw_host_sources(LSW_CORE_SOURCES ${LSW_CORE_CONCERNS})
set(CMAKE_AUTORCC ON)
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --always --dirty --abbrev=10
    OUTPUT_VARIABLE LSW_BUILD_ID ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/services/guest_bundle.cpp
    PROPERTIES COMPILE_DEFINITIONS "LSW_VERSION=\"${PROJECT_VERSION}\"")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/services/startup_header.cpp
    PROPERTIES COMPILE_DEFINITIONS "LSW_VERSION=\"${PROJECT_VERSION}\";LSW_BUILD_ID=\"${LSW_BUILD_ID}\"")
add_library(lsw_host_core STATIC ${LSW_CORE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/embedded/resources.qrc)
target_include_directories(lsw_host_core PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_include_directories(lsw_host_core PRIVATE $<BUILD_INTERFACE:${_core_embed}>)
target_link_libraries(lsw_host_core
    PUBLIC
        Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Xml Qt6::Concurrent
        PkgConfig::LIBVIRT
        LSW::Base
        LSW::LinkProto
        nlohmann_json::nlohmann_json
    PRIVATE
        LSW::CompileOptions
        PkgConfig::LIBVNCCLIENT
        PkgConfig::LIBDRM
        Vulkan::Vulkan
        PkgConfig::LIBVIRT_QEMU
        ZLIB::ZLIB
)
target_compile_definitions(lsw_host_core PRIVATE QT_NO_KEYWORDS)

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
        ZLIB::ZLIB
        PkgConfig::LIBISOFS
        ${WIMLIB_LIBRARY}
        ${HIVEX_LIBRARY}
)
target_compile_definitions(lsw_host_provision PRIVATE QT_NO_KEYWORDS)

lsw_host_sources(LSW_TOOLS_SOURCES tools)
add_library(lsw_host_tools STATIC ${LSW_TOOLS_SOURCES})
target_include_directories(lsw_host_tools PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw_host_tools
    PUBLIC  lsw_host_provision
    PRIVATE LSW::CompileOptions
)
target_compile_definitions(lsw_host_tools PRIVATE QT_NO_KEYWORDS)

lsw_host_sources(LSW_WIZARD_SOURCES wizard)
add_library(lsw_host_wizard STATIC ${LSW_WIZARD_SOURCES})
target_include_directories(lsw_host_wizard PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw_host_wizard
    PUBLIC  lsw_host_provision lsw_host_tools
    PRIVATE LSW::CompileOptions nlohmann_json::nlohmann_json
)
target_compile_definitions(lsw_host_wizard PRIVATE QT_NO_KEYWORDS)

lsw_host_sources(LSW_MANAGER_SOURCES manager mcp)
add_library(lsw_host_manager STATIC ${LSW_MANAGER_SOURCES})
target_include_directories(lsw_host_manager PUBLIC ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw_host_manager
    PUBLIC  lsw_host_core lsw_host_tools
    PRIVATE LSW::CompileOptions nlohmann_json::nlohmann_json
)
target_compile_definitions(lsw_host_manager PRIVATE QT_NO_KEYWORDS)
if(TARGET rwin_link)
    target_link_libraries(lsw_host_manager PRIVATE rwin::link)
    target_compile_definitions(lsw_host_manager PRIVATE LSW_HAVE_REMOTEWIN=1)
endif()

if(TARGET PkgConfig::XCB)
    target_link_libraries(lsw_host_manager PRIVATE PkgConfig::XCB)
    target_compile_definitions(lsw_host_manager PRIVATE LSW_HAVE_XCB=1)
endif()

lsw_host_sources(LSW_APP_SOURCES app)
add_executable(lsw-manager ${LSW_APP_SOURCES})
target_include_directories(lsw-manager PRIVATE ${LSW_HOST_INCLUDE_DIR})
target_link_libraries(lsw-manager
    PRIVATE LSW::CompileOptions lsw_host_wizard lsw_host_manager
)
target_compile_definitions(lsw-manager PRIVATE QT_NO_KEYWORDS)
install(TARGETS lsw-manager
        RUNTIME DESTINATION bin
        COMPONENT ${LSW_INSTALL_COMPONENT})

if(LSW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
