# Render-server library + executable.
#
# Layout (post-refactor):
#   server.{hpp,cpp}             accept loop driving a Listener + DispatchChainBuilder
#   connection.{hpp,cpp}         per-connection reader/replay threads on a FrameStream
#   session.hpp                  per-connection state (handshake, handle remap, ...)
#   handler.{hpp,cpp}            Handler interface + DispatchChain
#   handlers/hello_handler.*     HELLO/HELLO_ACK
#   handlers/vulkan_handler.*    routes into codegen-emitted ServerDispatch
#   handlers/batch_handler.*     unpacks a recorded command-buffer batch
#   handle_table.{hpp,cpp}       synthetic-handle remap (surfaces, externals)
#   vulkan/{instance,device}.*   RAII for real VkInstance / VkDevice
#   main.cpp                     argparse, signal handling, glues the above
#
# The library is transport-agnostic: pass any ``Listener`` (UnixListener
# today; VsockListener / TcpListener in later phases). The library is
# also dispatch-agnostic: pass any ``ServerDispatch`` to the
# DispatchChainBuilder (codegen RealVulkanDispatch by default, but stub /
# recording / replay-from-file backends drop in identically).

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

add_library(lsw_render STATIC
    src/server.cpp
    src/connection.cpp
    src/dispatch_chain.cpp
    src/handlers/hello_handler.cpp
    src/handlers/vulkan_handler.cpp
    src/handlers/surface_handler.cpp
    src/handlers/memory_handler.cpp
    src/handlers/batch_handler.cpp
    src/handlers/import_handler.cpp
    src/render_service.cpp
    src/handle_table.cpp
    src/surface_manager.cpp
    src/vulkan/instance.cpp
    src/vulkan/device.cpp
)
add_library(LSW::Render ALIAS lsw_render)

target_include_directories(lsw_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(lsw_render
    PUBLIC
        LSW::VkProto
        LSW::Protocol
        LSW::Base
        Vulkan::Vulkan
    PRIVATE
        LSW::CompileOptions
        PkgConfig::XCB
)

message(STATUS "lsw_render: real-Vulkan replay (Vulkan ${Vulkan_VERSION})")

add_executable(lsw-render-server src/main.cpp)
target_link_libraries(lsw-render-server PRIVATE LSW::Render LSW::CompileOptions)
