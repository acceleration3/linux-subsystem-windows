# lsw_linkproto — the host<->guest link contract, shared by both sides.
#
# Header-only and deliberately dependency-free: this is the ONE module
# compiled by both the Linux host toolchain and the mingw-w64 cross
# toolchain, so it may use nothing but the C++ standard library. No Qt,
# no spdlog, no POSIX, no Win32.
#
# It owns everything the manager and the guest agent must agree on:
#   envelope.hpp     the [type][len][payload] framing
#   byte_order.hpp   the big-endian codecs that framing uses
#   frame_reader.hpp stream reassembly (both ends run the same loop)
#   frame_ring.hpp   the IVSHMEM frame-ring layout, offsets static_asserted
#   commands.hpp     the RPC command names and event topics
#
# Membership rule: if one side could change it unilaterally and still
# compile, it does not belong here.

add_library(lsw_linkproto INTERFACE)
add_library(LSW::LinkProto ALIAS lsw_linkproto)

target_include_directories(lsw_linkproto
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(lsw_linkproto INTERFACE cxx_std_20)
