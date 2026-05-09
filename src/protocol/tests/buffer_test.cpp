// Minimal hand-rolled tests; we'll port to a real framework (Catch2 / GoogleTest)
// once the test surface grows. For Phase 0, no external test dep.

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

void test_pod_roundtrip() {
    using namespace lsw::protocol;
    Buffer b;
    b.write_pod<std::uint32_t>(0x11223344u);
    b.write_pod<std::uint64_t>(0xCAFEBABEDEADBEEFull);

    BufferReader r{b.bytes()};
    std::uint32_t x = 0;
    std::uint64_t y = 0;
    check(r.read_pod(x), "read u32");
    check(r.read_pod(y), "read u64");
    check(x == 0x11223344u, "u32 value");
    check(y == 0xCAFEBABEDEADBEEFull, "u64 value");
    check(r.eof(), "consumed all bytes");
}

void test_varint_roundtrip() {
    using namespace lsw::protocol;
    const std::uint64_t values[] = {
        0u, 1u, 127u, 128u, 16383u, 16384u, 0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFull,
    };
    for (auto v : values) {
        Buffer b;
        b.write_varint(v);
        BufferReader r{b.bytes()};
        std::uint64_t out = 0;
        check(r.read_varint(out), "read varint");
        check(out == v, "varint value");
        check(r.eof(), "varint fully consumed");
    }
}

void test_short_read_fails() {
    using namespace lsw::protocol;
    std::uint8_t bytes[2] = {0x01, 0x02};
    BufferReader r{bytes, sizeof(bytes)};
    std::uint32_t x = 0;
    check(!r.read_pod(x), "short read of u32 must fail");
    check(r.remaining() == 2, "reader unchanged on failed read");
}

void test_truncated_varint_fails() {
    using namespace lsw::protocol;
    // Continuation bit set on every byte = no terminator.
    std::uint8_t bytes[3] = {0x80, 0x80, 0x80};
    BufferReader r{bytes, sizeof(bytes)};
    std::uint64_t out = 0;
    check(!r.read_varint(out), "truncated varint must fail");
}

void test_frame_header_size() {
    using namespace lsw::protocol;
    static_assert(sizeof(FrameHeader) == 12);
    FrameHeader h{};
    h.cmd_id = kCmdPing;
    h.payload_len = 0;
    h.flags = FrameFlags::needs_reply;
    check(h.cmd_id == 3u, "kCmdPing == 3");
    check((h.flags & FrameFlags::needs_reply) != 0u, "flag bit set");
}

}  // namespace

int main() {
    test_pod_roundtrip();
    test_varint_roundtrip();
    test_short_read_fails();
    test_truncated_varint_fails();
    test_frame_header_size();
    std::puts("OK");
    return 0;
}
