# lsw_protocol — framing, channels, transports.
#
# The module is layered so a Windows build can take the part it needs and
# leave the rest:
#
#   frame_channel.hpp    the seam. Frames in, frames out, transport
#                        unnamed.
#   shmem_frame_channel  frames over a duplex ring pair in a SharedRegion.
#                        Portable: it is memcpy and fences, nothing else.
#   fd_frame_channel     frames over a stream socket. POSIX.
#   channel_transport    the client side of any channel, with the
#                        token->waiter threading model.
#
# The POSIX sources are listed separately because D3 needs the ICD to
# cross-compile, and with B done it needs only the shared-memory half.
set(LSW_PROTOCOL_PORTABLE_SOURCES
    src/channel_transport.cpp
    src/doorbell.cpp
    src/endpoint.cpp
    src/region_arena.cpp
    src/shmem_frame_channel.cpp
)

set(LSW_PROTOCOL_POSIX_SOURCES
    src/endpoint_posix.cpp
    src/fd_frame_channel.cpp
    src/frame_stream.cpp
    src/posix_shm_region.cpp
    src/shmem_endpoint.cpp
    src/unix_endpoint.cpp
)

set(LSW_PROTOCOL_WIN32_SOURCES
    src/endpoint_win32.cpp
    src/ivshmem_region.cpp
)

# On Windows only the portable half is compiled. The guest ICD reaches
# its region through IVSHMEM, not through shm_open, and it has no
# ``unix://`` peer to talk to — so the sockets and the POSIX mapper are
# genuinely absent rather than stubbed out. That is D3's real content:
# with B done, the ICD needs only ShmemFrameChannel, and
# ShmemFrameChannel is memcpy and fences.
if(WIN32)
    add_library(lsw_protocol STATIC
        ${LSW_PROTOCOL_PORTABLE_SOURCES}
        ${LSW_PROTOCOL_WIN32_SOURCES}
    )
else()
    add_library(lsw_protocol STATIC
        ${LSW_PROTOCOL_PORTABLE_SOURCES}
        ${LSW_PROTOCOL_POSIX_SOURCES}
    )
endif()
add_library(LSW::Protocol ALIAS lsw_protocol)

target_include_directories(lsw_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# LinkProto carries the ring, the region layout and the channel table —
# the contract both sides of the VM boundary compile. ShmemFrameChannel
# builds on it rather than growing a second ring.
target_link_libraries(lsw_protocol
    PUBLIC LSW::CompileOptions LSW::LinkProto
)

find_package(Threads REQUIRED)
target_link_libraries(lsw_protocol PUBLIC Threads::Threads)

# setupapi: enumerating the IVSHMEM device interface.
if(WIN32)
    target_link_libraries(lsw_protocol PUBLIC setupapi)
endif()

# POSIX shm needs librt on older glibc; harmless where it is built in.
if(UNIX AND NOT APPLE)
    target_link_libraries(lsw_protocol PUBLIC rt)
endif()

# Tests are registered from the top-level CMakeLists (src/protocol/tests),
# which is guarded by LSW_BUILD_TESTS there.
