// Tests for the contract shared by the manager and the guest agent.
//
// This is the code with the worst failure mode in the project: both ends
// of a VM boundary must agree byte for byte, and when they stop agreeing
// nothing fails loudly — frames come out garbled or a command silently
// does not exist. It used to be untestable because each side had its own
// copy inside a platform-specific TU. Now it is one header pair, so it
// gets tested host-native regardless of which side ships it.
//
// Same hand-rolled style as the other tests in this repo: no external
// test dependency.

#include "lsw/linkproto/byte_order.hpp"
#include "lsw/linkproto/byte_ring.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/frame_reader.hpp"
#include "lsw/linkproto/region.hpp"
#include "lsw/linkproto/region_layout.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

int g_checks = 0;

void check(bool ok, const char* label) {
    ++g_checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

// ---- byte order ---------------------------------------------------------

void test_byte_order_roundtrip() {
    using namespace lsw::linkproto;
    std::uint8_t buf[8] = {};

    write_u32_be(buf, 0x11223344U);
    check(buf[0] == 0x11 && buf[1] == 0x22 && buf[2] == 0x33 && buf[3] == 0x44,
          "u32 is written most-significant byte first");
    check(read_u32_be(buf) == 0x11223344U, "u32 round-trips");

    write_u64_be(buf, 0xCAFEBABEDEADBEEFULL);
    check(buf[0] == 0xCA && buf[7] == 0xEF, "u64 is written most-significant byte first");
    check(read_u64_be(buf) == 0xCAFEBABEDEADBEEFULL, "u64 round-trips");

    write_u32_be(buf, 0);
    check(read_u32_be(buf) == 0, "zero round-trips");
    write_u32_be(buf, 0xFFFFFFFFU);
    check(read_u32_be(buf) == 0xFFFFFFFFU, "max u32 round-trips");
}

// ---- envelope framing ---------------------------------------------------

void test_envelope_header() {
    using namespace lsw::linkproto;
    check(kHeaderSize == 5, "header is type + u32 length");

    std::uint8_t header[kHeaderSize] = {};
    write_header(header, FrameType::Control, 8);
    check(read_type(header) == FrameType::Control, "type round-trips");
    check(read_payload_len(header) == 8, "length round-trips");

    // The retired types stay burnt: an old peer's frames must never be
    // mistaken for a live one.
    check(static_cast<int>(FrameType::Control) == 1, "control is type 1");
    check(static_cast<int>(FrameType::RetiredFrame) == 2, "type 2 stays reserved");
    check(static_cast<int>(FrameType::RetiredFrameGone) == 3, "type 3 stays reserved");
}

// Append one framed message to ``out``.
void append_envelope(std::vector<std::uint8_t>& out, lsw::linkproto::FrameType type,
                     const std::string& payload) {
    std::uint8_t header[lsw::linkproto::kHeaderSize] = {};
    lsw::linkproto::write_header(header, type, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), header, header + sizeof(header));
    out.insert(out.end(), payload.begin(), payload.end());
}

struct Received {
    lsw::linkproto::FrameType type;
    std::string payload;
};

void test_frame_reader_whole_frames() {
    using namespace lsw::linkproto;
    std::vector<std::uint8_t> wire;
    append_envelope(wire, FrameType::Control, R"({"id":1})");
    append_envelope(wire, FrameType::Control, R"({"id":2})");

    FrameReader reader;
    reader.append(wire.data(), wire.size());

    std::vector<Received> got;
    const bool ok = reader.drain([&](FrameType t, const std::uint8_t* p, std::size_t n) {
        got.push_back({t, std::string(reinterpret_cast<const char*>(p), n)});
        return true;
    });
    check(ok, "drain reports the stream is still healthy");
    check(got.size() == 2, "both frames delivered");
    check(got[0].payload == R"({"id":1})", "first payload intact");
    check(got[1].payload == R"({"id":2})", "second payload intact");
    check(reader.buffered() == 0, "nothing left buffered");
}

void test_frame_reader_byte_at_a_time() {
    using namespace lsw::linkproto;
    // The real hazard: TCP hands over arbitrary fragments, so a frame can
    // arrive split anywhere — including inside the 5-byte header.
    std::vector<std::uint8_t> wire;
    append_envelope(wire, FrameType::Control, R"({"cmd":"system_info"})");

    FrameReader reader;
    std::vector<Received> got;
    for (std::size_t i = 0; i < wire.size(); ++i) {
        reader.append(&wire[i], 1);
        (void)reader.drain([&](FrameType t, const std::uint8_t* p, std::size_t n) {
            got.push_back({t, std::string(reinterpret_cast<const char*>(p), n)});
            return true;
        });
        if (i + 1 < wire.size()) {
            check(got.empty(), "no frame is delivered before its last byte arrives");
        }
    }
    check(got.size() == 1, "frame delivered once complete");
    check(got[0].payload == R"({"cmd":"system_info"})", "payload intact across fragmentation");
}

void test_frame_reader_keeps_partial_tail() {
    using namespace lsw::linkproto;
    std::vector<std::uint8_t> wire;
    append_envelope(wire, FrameType::Control, "AAAA");
    append_envelope(wire, FrameType::Control, "BBBB");
    wire.resize(wire.size() - 2);  // truncate the second payload

    FrameReader reader;
    reader.append(wire.data(), wire.size());
    int delivered = 0;
    (void)reader.drain([&](FrameType, const std::uint8_t*, std::size_t) {
        ++delivered;
        return true;
    });
    check(delivered == 1, "only the complete frame is delivered");
    check(reader.buffered() == lsw::linkproto::kHeaderSize + 2,
          "the partial frame stays buffered whole");

    const std::uint8_t rest[2] = {'B', 'B'};
    reader.append(rest, sizeof(rest));
    (void)reader.drain([&](FrameType, const std::uint8_t*, std::size_t) {
        ++delivered;
        return true;
    });
    check(delivered == 2, "the frame completes once its tail arrives");
}

void test_frame_reader_aborts_on_handler_false() {
    using namespace lsw::linkproto;
    std::vector<std::uint8_t> wire;
    append_envelope(wire, FrameType::Control, "one");
    append_envelope(wire, FrameType::Control, "two");

    FrameReader reader;
    reader.append(wire.data(), wire.size());
    int delivered = 0;
    const bool ok = reader.drain([&](FrameType, const std::uint8_t*, std::size_t) {
        ++delivered;
        return false;  // the connection is going away
    });
    check(!ok, "drain reports the abort");
    check(delivered == 1, "no further frames are handed over after an abort");
}

void test_frame_reader_reset() {
    using namespace lsw::linkproto;
    FrameReader reader;
    const std::uint8_t junk[3] = {1, 2, 3};
    reader.append(junk, sizeof(junk));
    check(reader.buffered() == 3, "partial header buffered");
    reader.reset();
    check(reader.buffered() == 0, "reset drops a half-received frame");
}

void test_command_names_unique() {
    namespace cmd = lsw::linkproto::cmd;
    namespace topic = lsw::linkproto::topic;

    // Duplicate names would mean two handlers fighting for one command
    // (the agent's registry aborts on that) or a topic silently shadowing
    // another.
    const std::vector<const char*> commands = {
        cmd::kSystemInfo,     cmd::kListApps,       cmd::kLaunch,
        cmd::kGetIcon,        cmd::kListWindows,    cmd::kCloseWindow,
        cmd::kFocusWindow,    cmd::kUnfocusWindow,  cmd::kResizeWindow,
        cmd::kWindowProps,    cmd::kNcHitTest,      cmd::kGetWindowIcon,
        cmd::kInjectInput,    cmd::kSubscribeWindow, cmd::kUnsubscribeWindow,
        cmd::kCaptureDebug,   cmd::kDownloadInstall,
        cmd::kDownloadExtract, cmd::kRunScript,     cmd::kConsoleOpen,
        cmd::kConsoleInput,   cmd::kConsoleClose,   cmd::kUiaState,
        cmd::kUiaInvoke,      cmd::kUiaSetValue,    cmd::kUiaGetText,
        cmd::kUiaTreeDump,    cmd::kUiaSubscribeLifecycle,
        cmd::kUiaUnsubscribeLifecycle, cmd::kUiaSubscribeFocus,
        cmd::kUiaSubscribeProperties, cmd::kUiaUnsubscribe,
    };
    std::set<std::string> seen;
    for (const char* name : commands) {
        check(name != nullptr && *name != '\0', "no empty command name");
        check(seen.insert(name).second, "no duplicate command name");
    }

    const std::vector<const char*> topics = {
        topic::kLog,             topic::kSetupProgress,  topic::kSetupDone,
        topic::kConsoleOutput,   topic::kConsoleClosed,  topic::kCaptionDragStarted,
        topic::kUiaWindowOpened, topic::kUiaWindowClosed, topic::kUiaFocusChanged,
        topic::kUiaStateChanged, topic::kUiaTitleChanged, topic::kUiaModalChanged,
    };
    std::set<std::string> topic_seen;
    for (const char* name : topics) {
        check(name != nullptr && *name != '\0', "no empty topic name");
        check(topic_seen.insert(name).second, "no duplicate topic name");
    }
}

// ---- byte ring ----------------------------------------------------------

// A ring lives in raw shared memory; here that is just a buffer.
struct RingFixture {
    std::vector<std::uint8_t> memory;
    lsw::linkproto::ByteRingProducer producer;
    lsw::linkproto::ByteRingConsumer consumer;

    explicit RingFixture(std::size_t bytes) : memory(bytes, 0) {
        check(lsw::linkproto::ring_init(memory.data(), bytes), "ring initialises");
        producer = lsw::linkproto::ByteRingProducer{memory.data()};
        consumer = lsw::linkproto::ByteRingConsumer{memory.data()};
    }
};

bool write_str(lsw::linkproto::ByteRingProducer& p, const std::string& s) {
    return p.write(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

std::string read_str(lsw::linkproto::ByteRingConsumer& c) {
    std::vector<std::uint8_t> buf(c.max_message());
    const std::size_t n = c.read(buf.data(), buf.size());
    return std::string(reinterpret_cast<const char*>(buf.data()), n);
}

void test_ring_init() {
    using namespace lsw::linkproto;
    // Capacity is the largest power of two that fits after the header.
    RingFixture ring{4096};
    check(ring.producer.valid() && ring.consumer.valid(), "both endpoints bind");
    check(ring.producer.capacity() == 2048, "capacity is a power of two under the area");
    check((ring.producer.capacity() & (ring.producer.capacity() - 1)) == 0,
          "capacity is a power of two");
    check(ring.consumer.empty(), "a fresh ring is empty");

    // An area too small for a usable ring is refused rather than
    // producing an endpoint that computes garbage indices.
    std::vector<std::uint8_t> tiny(sizeof(ByteRingHeader) + 8, 0);
    check(!ring_init(tiny.data(), tiny.size()), "a too-small area is refused");
    check(!ByteRingProducer{tiny.data()}.valid(), "an uninitialised ring is invalid");
}

void test_ring_fifo() {
    RingFixture ring{4096};
    check(write_str(ring.producer, "one"), "write one");
    check(write_str(ring.producer, "two"), "write two");
    check(write_str(ring.producer, "three"), "write three");
    check(!ring.consumer.empty(), "ring reports pending data");
    check(read_str(ring.consumer) == "one", "messages come back in order");
    check(read_str(ring.consumer) == "two", "second message");
    check(read_str(ring.consumer) == "three", "third message");
    check(ring.consumer.empty(), "ring drains to empty");
    check(read_str(ring.consumer).empty(), "reading an empty ring yields nothing");
}

void test_ring_peek_and_empty_payload() {
    RingFixture ring{4096};
    check(ring.consumer.peek_size() == 0, "peek on an empty ring is 0");
    check(write_str(ring.producer, "abcdef"), "write");
    check(ring.consumer.peek_size() == 6, "peek reports the message size");
    check(!ring.consumer.empty(), "peek does not consume");
    check(read_str(ring.consumer) == "abcdef", "message survives the peek");

    // A zero-length message is still a message: it must advance the ring.
    check(ring.producer.write(nullptr, 0), "zero-length write accepted");
    check(!ring.consumer.empty(), "zero-length message is pending");
    check(read_str(ring.consumer).empty(), "zero-length message reads back empty");
    check(ring.consumer.empty(), "and the ring is drained");
}

void test_ring_wraps() {
    // The real hazard: a message that straddles the end of the buffer.
    // Write and drain repeatedly with a size that does not divide the
    // capacity, so every offset is exercised.
    RingFixture ring{4096};
    const std::string payload(100, 'x');
    for (int i = 0; i < 200; ++i) {
        check(write_str(ring.producer, payload), "write across the wrap point");
        const std::string got = read_str(ring.consumer);
        if (got != payload) {
            check(false, "message survives wrapping");
        }
    }
    check(ring.consumer.empty(), "ring ends empty after wrapping");

    // Interleave two in flight at a time so a record header itself lands
    // across the wrap.
    for (int i = 0; i < 200; ++i) {
        check(write_str(ring.producer, payload), "first in flight");
        check(write_str(ring.producer, payload), "second in flight");
        check(read_str(ring.consumer) == payload, "first out");
        check(read_str(ring.consumer) == payload, "second out");
    }
}

void test_ring_full_is_all_or_nothing() {
    RingFixture ring{4096};
    const std::string payload(64, 'y');
    int written = 0;
    while (write_str(ring.producer, payload)) {
        ++written;
        if (written > 1000) {
            check(false, "ring should have filled");
            break;
        }
    }
    check(written > 0, "the ring accepted messages before filling");
    check(ring.producer.dropped() >= 1, "a refused write is counted as dropped");

    // Everything accepted must still read back intact — a full ring must
    // not have corrupted the tail.
    for (int i = 0; i < written; ++i) {
        if (read_str(ring.consumer) != payload) {
            check(false, "message intact after the ring filled");
        }
    }
    check(ring.consumer.empty(), "drained");
    // And the ring is usable again.
    check(write_str(ring.producer, payload), "ring recovers after draining");
}

void test_ring_oversized_message_refused() {
    using namespace lsw::linkproto;
    RingFixture ring{4096};
    const std::string too_big(ring.producer.max_message() + 1, 'z');
    check(!write_str(ring.producer, too_big), "a message larger than the cap is refused");
    check(ring.consumer.empty(), "and nothing was written");
    const std::string just_right(ring.producer.max_message(), 'z');
    check(write_str(ring.producer, just_right), "the largest allowed message fits");
    check(read_str(ring.consumer) == just_right, "and reads back");
}

void test_ring_survives_desync() {
    using namespace lsw::linkproto;
    // Corrupt the ring the way a crashed peer would: claim a record
    // longer than what was published. The consumer must resynchronise
    // rather than wedge, because a wedged ring never recovers.
    RingFixture ring{4096};
    check(write_str(ring.producer, "hello"), "write");
    auto* header = ring.memory.data() + sizeof(ByteRingHeader);
    const std::uint32_t bogus = 9999;
    std::memcpy(header, &bogus, sizeof(bogus));

    std::vector<std::uint8_t> buf(ring.consumer.max_message());
    check(ring.consumer.read(buf.data(), buf.size()) == 0, "the bad record yields nothing");
    check(ring.consumer.empty(), "the consumer resynchronised instead of wedging");
    check(write_str(ring.producer, "after"), "the ring is usable again");
    check(read_str(ring.consumer) == "after", "and carries messages again");
}

// ---- region / channels --------------------------------------------------

void test_region_layout() {
    using namespace lsw::linkproto;
    check(sizeof(RegionHeader) == 64, "region header stays 64 bytes");
    check(sizeof(ChannelDescriptor) == 32, "channel entries stay 32 bytes");
    check(kChannelTableOffset == 0x40, "channel table follows the header");
    check(kChannelTableOffset + (kMaxChannels * sizeof(ChannelDescriptor))
              <= kFirstChannelOffset,
          "the table fits before the first channel");
    check(kRpcDoorbellVector < kDoorbellVectorCount,
          "the message channel's doorbell is one the device exposes");
}

void test_channel_table_roundtrip() {
    using namespace lsw::linkproto;
    std::vector<std::uint8_t> region(kFirstChannelOffset + 4096, 0);
    auto* base = region.data();

    // An unwritten region must not appear to contain channels — the
    // guest maps this memory before the manager may have finished.
    check(!find_channel(base, kChannelRpc).valid(), "no channels before the table exists");

    store_field<std::uint32_t>(base + region_off::magic, kRegionMagic);
    store_field<std::uint32_t>(base + region_off::version, kRegionVersion);
    store_field<std::uint32_t>(base + region_off::channel_count, 2U);
    store_field<std::uint32_t>(base + region_off::channel_entry_size,
                               static_cast<std::uint32_t>(sizeof(ChannelDescriptor)));
    store_field<std::uint64_t>(base + region_off::channel_table_offset, kChannelTableOffset);
    store_field<std::uint64_t>(base + region_off::region_size, region.size());
    // Written second, and found by kind rather than by position.
    write_channel(base, 0, 999, 0x6000, 0x8000);
    write_channel(base, 1, kChannelRpc, 0x1000, 0x1000);

    const auto rpc = find_channel(base, kChannelRpc);
    check(rpc.valid() && rpc.offset == 0x1000 && rpc.size == 0x1000,
          "the message channel is found by kind, not position");
    check(!find_channel(base, 1234).valid(), "an unknown kind is simply absent");

    // A table written by a future build with bigger entries must be
    // refused, not misread.
    store_field<std::uint32_t>(base + region_off::channel_entry_size, 64U);
    check(!find_channel(base, kChannelRpc).valid(), "a foreign entry size is refused");
}

// B5's region parity check, in its strictest form: two regions laid out
// independently at the size the manager gives a VM must agree byte for
// byte on the header and the channel table.
//
// The manager and the local loopback both reach ``lay_out_region``, so
// this is really asserting that the layout is a pure function of the
// size — no time, no address, no environment. If it ever stops being
// one, "the same code runs in the VM" quietly becomes false in the way
// that is hardest to see, because both halves still work.
void test_region_layout_is_deterministic() {
    using namespace lsw::linkproto;

    const std::uint64_t size = kDefaultRegionBytes;
    const auto plan = plan_region(size);
    check(plan.valid, "the default region size lays out");
    const auto touched = static_cast<std::size_t>(plan.rpc.offset + plan.rpc.size);

    std::vector<std::uint8_t> a(touched, 0);
    std::vector<std::uint8_t> b(touched, 0);
    const auto la = lay_out_region(a.data(), size);
    const auto lb = lay_out_region(b.data(), size);
    check(la.valid && lb.valid, "both regions laid out");
    check(std::memcmp(a.data(), b.data(), touched) == 0,
          "two independently laid-out regions are byte-identical");

    // And the table describes what the arithmetic said it would.
    check(find_channel(a.data(), kChannelRpc).offset == plan.rpc.offset, "rpc where planned");

    // And the region size must stay a power of two: it is the size of
    // the ivshmem PCI BAR, and PCI BARs are powers of two.
    check((kDefaultRegionBytes & (kDefaultRegionBytes - 1)) == 0,
          "the region size is a power of two (it backs a PCI BAR)");
    check(region_is_readable(a.data(), size), "a laid-out region reads back as valid");

    // A region too small must be refused rather than laid out short.
    std::vector<std::uint8_t> tiny(kFirstChannelOffset + 16, 0);
    check(!lay_out_region(tiny.data(), tiny.size()).valid, "a too-small region is refused");
    check(!region_is_readable(tiny.data(), tiny.size()), "and does not read back as valid");
}

void test_duplex_split() {
    using namespace lsw::linkproto;
    constexpr std::uint64_t kChannelSize = 1 << 20;
    check(guest_to_host_ring_offset(kChannelSize) == 0, "guest->host ring comes first");
    check(host_to_guest_ring_offset(kChannelSize) == kChannelSize / 2,
          "host->guest ring takes the second half");
    check(duplex_ring_bytes(kChannelSize) == kChannelSize / 2, "each ring gets half");
    // The halves must not overlap, or the two directions would corrupt
    // each other.
    check(guest_to_host_ring_offset(kChannelSize) + duplex_ring_bytes(kChannelSize)
              <= host_to_guest_ring_offset(kChannelSize),
          "the two rings are disjoint");
}

void test_duplex_channel_end_to_end() {
    using namespace lsw::linkproto;
    // Both directions of one channel, wired the way the manager and the
    // agent wire them.
    constexpr std::uint64_t kChannelSize = 8192;
    std::vector<std::uint8_t> channel(kChannelSize, 0);
    check(ring_init(channel.data() + guest_to_host_ring_offset(kChannelSize),
                    duplex_ring_bytes(kChannelSize)),
          "guest->host ring initialises");
    check(ring_init(channel.data() + host_to_guest_ring_offset(kChannelSize),
                    duplex_ring_bytes(kChannelSize)),
          "host->guest ring initialises");

    ByteRingProducer guest_out{channel.data() + guest_to_host_ring_offset(kChannelSize)};
    ByteRingConsumer host_in{channel.data() + guest_to_host_ring_offset(kChannelSize)};
    ByteRingProducer host_out{channel.data() + host_to_guest_ring_offset(kChannelSize)};
    ByteRingConsumer guest_in{channel.data() + host_to_guest_ring_offset(kChannelSize)};

    check(write_str(guest_out, R"({"event":"channel_hello"})"), "guest announces");
    check(guest_in.empty(), "the announcement did not land in the wrong direction");
    check(read_str(host_in) == R"({"event":"channel_hello"})", "host receives it");

    check(write_str(host_out, R"({"id":7,"cmd":"system_info"})"), "host requests");
    check(host_in.empty(), "the request did not land in the wrong direction");
    check(read_str(guest_in) == R"({"id":7,"cmd":"system_info"})", "guest receives it");
}

}  // namespace

int main() {
    test_byte_order_roundtrip();
    test_envelope_header();
    test_frame_reader_whole_frames();
    test_frame_reader_byte_at_a_time();
    test_frame_reader_keeps_partial_tail();
    test_frame_reader_aborts_on_handler_false();
    test_frame_reader_reset();
    test_command_names_unique();
    test_ring_init();
    test_ring_fifo();
    test_ring_peek_and_empty_payload();
    test_ring_wraps();
    test_ring_full_is_all_or_nothing();
    test_ring_oversized_message_refused();
    test_ring_survives_desync();
    test_region_layout();
    test_channel_table_roundtrip();
    test_region_layout_is_deterministic();
    test_duplex_split();
    test_duplex_channel_end_to_end();
    std::printf("linkproto: %d checks passed\n", g_checks);
    return 0;
}
