add_library(suite_support OBJECT
    api_snapshot.cpp
    artifact_writer.cpp
    av_adapters.cpp
    child_runs.cpp
    compare.cpp
    display_devices.cpp
    driver_registry.cpp
    event_log_scan.cpp
    file_version.cpp
    kmt_api.cpp
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
target_link_libraries(suite_support PUBLIC suite_core version shcore dwmapi)
