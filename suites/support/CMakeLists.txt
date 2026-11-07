add_library(suite_support OBJECT
    api_policy.cpp
    api_snapshot.cpp
    d3d11_backend.cpp
    d3d9_rig.cpp
    d3d9_tools.cpp
    d3d9_twin.cpp
    d3d11_device.cpp
    d3d11_textures.cpp
    d3d11_tools.cpp
    format_codec.cpp
    oracle_rig.cpp
    swap_rig.cpp
    d3d12_backend.cpp
    d3d12_device.cpp
    d3d9_backend.cpp
    dxgi_budget.cpp
    gl_backend.cpp
    gpu_backends.cpp
    opencl_backend.cpp
    process_sampler.cpp
    stress_load.cpp
    target_adapter.cpp
    vulkan_backend.cpp
    vulkan_device.cpp
    artifact_writer.cpp
    backend_harness.cpp
    av_adapters.cpp
    child_runs.cpp
    compare.cpp
    display_devices.cpp
    driver_registry.cpp
    event_log_scan.cpp
    file_version.cpp
    kmt_api.cpp
    memory_tools.cpp
    leak_loop.cpp
    mismatch.cpp
    opencl_api.cpp
    opengl_context.cpp
    pattern.cpp
    png_writer.cpp
    registry_reader.cpp
    test_window.cpp
    vulkan_api.cpp
)
set(LSW_SUITES_KHRONOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external/accel-virt/third_party/khronos"
    CACHE PATH "Khronos headers root (contains include/CL and include/vulkan)")
target_include_directories(suite_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(suite_support SYSTEM PUBLIC ${LSW_SUITES_KHRONOS_DIR}/include)
target_link_libraries(suite_support PUBLIC suite_core version shcore dwmapi psapi d3d9 d3d11 d3d12)
