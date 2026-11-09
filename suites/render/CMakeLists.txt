file(GLOB RENDER_COMMON CONFIGURE_DEPENDS common/*.cpp)
file(GLOB RENDER_PLATFORM CONFIGURE_DEPENDS platform/win32/*.cpp)
file(GLOB RENDER_HOST CONFIGURE_DEPENDS host/*.cpp)
file(GLOB RENDER_D3D CONFIGURE_DEPENDS d3d/*.cpp)
file(GLOB RENDER_D3D9 CONFIGURE_DEPENDS d3d9/*.cpp)
file(GLOB RENDER_D3D10 CONFIGURE_DEPENDS d3d10/*.cpp)
file(GLOB RENDER_D3D11 CONFIGURE_DEPENDS d3d11/*.cpp)
file(GLOB RENDER_D3D12 CONFIGURE_DEPENDS d3d12/*.cpp)
file(GLOB RENDER_VK CONFIGURE_DEPENDS vk/*.cpp vk/spirv/gen/*.cpp)
file(GLOB RENDER_GL CONFIGURE_DEPENDS gl/*.cpp)

add_library(suite_render OBJECT
    render.cpp
    ${RENDER_COMMON}
    ${RENDER_PLATFORM}
    ${RENDER_HOST}
    ${RENDER_D3D}
    ${RENDER_D3D9}
    ${RENDER_D3D10}
    ${RENDER_D3D11}
    ${RENDER_D3D12}
    ${RENDER_VK}
    ${RENDER_GL}
)
target_include_directories(suite_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(suite_render PUBLIC suite_core suite_support LSW::CompileOptions)
