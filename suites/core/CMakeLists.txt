add_library(suite_core OBJECT
    check_filter.cpp
    context.cpp
    child_protocol.cpp
    glob.cpp
    json.cpp
    json_report.cpp
    options.cpp
    registry.cpp
    report_printer.cpp
    run_guard.cpp
    runner.cpp
    single_check.cpp
    text.cpp
    worker.cpp
    dxgi_adapters.cpp
)
target_include_directories(suite_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(suite_core PUBLIC LSW::CompileOptions dxgi setupapi cfgmgr32)
