#include "lsw/linkproto/byte_order.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/file_name.hpp"
#include "lsw/linkproto/frame_reader.hpp"

#include <cstdio>
#include <cstdlib>
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

void test_envelope_header() {
    using namespace lsw::linkproto;
    check(kHeaderSize == 5, "header is type + u32 length");

    std::uint8_t header[kHeaderSize] = {};
    write_header(header, FrameType::Control, 8);
    check(read_type(header) == FrameType::Control, "type round-trips");
    check(read_payload_len(header) == 8, "length round-trips");
    check(static_cast<int>(FrameType::Control) == 1, "control is type 1");
}

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
    wire.resize(wire.size() - 2);

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
        return false;
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

    const std::vector<const char*> commands = {
        cmd::kSystemInfo,     cmd::kListApps,       cmd::kLaunch,
        cmd::kGetIcon,        cmd::kListWindows,    cmd::kCloseWindow,
        cmd::kWindowCommand,  cmd::kStartWindowMirror,
        cmd::kDownloadInstall,
        cmd::kDownloadExtract, cmd::kRunScript,     cmd::kConsoleOpen,
        cmd::kConsoleInput,   cmd::kConsoleClose,   cmd::kUiaState,
        cmd::kUiaInvoke,      cmd::kUiaSetValue,    cmd::kUiaGetText,
        cmd::kUiaTreeDump,    cmd::kUiaSubscribeLifecycle,
        cmd::kUiaSubscribeFocus,
        cmd::kUiaSubscribeProperties, cmd::kUiaUnsubscribe,
        cmd::kGpuStatus,      cmd::kSetDisplay,     cmd::kListDir,
        cmd::kResolveShortcut, cmd::kListSessions, cmd::kSetLogLevel,
        cmd::kReadFile,       cmd::kPackLogs,
    };
    std::set<std::string> seen;
    for (const char* name : commands) {
        check(name != nullptr && *name != '\0', "no empty command name");
        check(seen.insert(name).second, "no duplicate command name");
    }

    const std::vector<const char*> topics = {
        topic::kLog,             topic::kSetupProgress,  topic::kSetupDone,
        topic::kConsoleOutput,   topic::kConsoleClosed,
        topic::kUiaWindowOpened, topic::kUiaWindowClosed, topic::kUiaFocusChanged,
        topic::kUiaStateChanged, topic::kUiaTitleChanged, topic::kUiaModalChanged,
    };
    std::set<std::string> topic_seen;
    for (const char* name : topics) {
        check(name != nullptr && *name != '\0', "no empty topic name");
        check(topic_seen.insert(name).second, "no duplicate topic name");
    }
}

}

void test_file_names() {
    using lsw::linkproto::WindowsFileName;
    check(WindowsFileName::legal("report.xlsx"), "an ordinary name is legal");
    check(WindowsFileName::legal("100%.txt") && WindowsFileName::legal("hello!.docx"),
          "% and ! are legal names");
    check(WindowsFileName::legal("\xc3\xa9t\xc3\xa9.txt"), "non-ASCII is legal");
    for (const char* bad : {"", "a:b", "a?b", "a*b", "a<b", "a>b", "a|b", "a\"b", "a/b", "a\\b", "a.", "a ",
                            "a\tb", "CON", "nul.txt", "Com1.log", "LPT9", "CONIN$"}) {
        check(!WindowsFileName::legal(bad), "an illegal or reserved name is refused");
    }
    check(WindowsFileName::legal("COM0") && WindowsFileName::legal("console.txt"), "COM0 and console are not reserved");
    check(!WindowsFileName::legal(std::string(241, 'a')), "too long is refused");
    check(WindowsFileName::sanitised("report.xlsx") == "report.xlsx", "a legal name is unchanged");
    check(WindowsFileName::sanitised("a:b?.txt") == "a_b_.txt", "illegal characters become _");
    check(WindowsFileName::sanitised("name. ") == "name", "trailing dots and spaces go");
    check(WindowsFileName::sanitised("nul.txt") == "_nul.txt", "a device name gets a _ in front");
    check(WindowsFileName::sanitised("") == "file" && WindowsFileName::sanitised("...") == "file",
          "an empty name becomes file");
    const std::string cut = WindowsFileName::sanitised(std::string(300, 'a') + ".docx");
    check(cut.size() <= 240 && cut.ends_with(".docx") && WindowsFileName::legal(cut),
          "a long name is cut, its extension kept");
    check(WindowsFileName::legal(WindowsFileName::sanitised("x\xc3\xa9" + std::string(300, 'y'))),
          "whatever comes out is legal");
}

int main() {
    test_byte_order_roundtrip();
    test_envelope_header();
    test_frame_reader_whole_frames();
    test_frame_reader_byte_at_a_time();
    test_frame_reader_keeps_partial_tail();
    test_frame_reader_aborts_on_handler_false();
    test_frame_reader_reset();
    test_command_names_unique();
    test_file_names();
    std::printf("linkproto: %d checks passed\n", g_checks);
    return 0;
}
