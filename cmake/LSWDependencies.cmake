find_package(spdlog QUIET CONFIG)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(spdlog
        URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
        URL_HASH SHA256=9962648c9b4f1a7bbc76fd8d9172555bad1871fdb14ff4f842ef87949682caa5
    )
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
    set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
    set(SPDLOG_INSTALL       OFF CACHE INTERNAL "")
    set(SPDLOG_BUILD_SHARED  OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(spdlog)
    message(STATUS "spdlog: fetched (no system package found)")
endif()

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

if(WIN32)
    set(LSW_VULKAN_LOADER_TAG "v1.4.357" CACHE STRING
        "Vulkan-Loader tag to fetch for the guest")
    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        ${LSW_VULKAN_LOADER_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(VulkanHeaders)
    string(REGEX REPLACE "^v" "" _lsw_vulkan_version "${LSW_VULKAN_LOADER_TAG}")
    set(VulkanHeaders_VERSION "${_lsw_vulkan_version}")
    FetchContent_Declare(VulkanLoader
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader.git
        GIT_TAG        ${LSW_VULKAN_LOADER_TAG}
        GIT_SHALLOW    TRUE
    )
    set(BUILD_TESTS  OFF CACHE INTERNAL "")
    set(UPDATE_DEPS  OFF CACHE INTERNAL "")
    set(BUILD_WSI_XCB_SUPPORT     OFF CACHE INTERNAL "")
    set(BUILD_WSI_XLIB_SUPPORT    OFF CACHE INTERNAL "")
    set(BUILD_WSI_WAYLAND_SUPPORT OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(VulkanLoader)

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_lsw_loader_arch x86)
    else()
        set(_lsw_loader_arch x64)
    endif()
    install(FILES $<TARGET_FILE:vulkan>
            DESTINATION ${LSW_INSTALL_GUESTDIR}/loaders/${_lsw_loader_arch}
            COMPONENT ${LSW_INSTALL_COMPONENT})
endif()
