// Host-native tests for the guest agent's platform-free logic.
//
// The agent only links on Windows, so historically none of it was
// tested at all — including the parts that are plain C++ and have
// nothing to do with Win32. Those TUs are compiled directly into this
// executable by the host toolchain (see the sibling CMakeLists), which
// costs nothing and covers the string handling most likely to be wrong
// in a way a running VM would only show as a weird filename.
//
// Same hand-rolled style as the other tests in this repo.

#include "lsw/agent/core/command_line.hpp"
#include "lsw/agent/install/job_id_generator.hpp"
#include "lsw/agent/install/url_file_name.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/session/pending_calls.hpp"
#include "lsw/agent/session/pipe_codec.hpp"
#include "lsw/agent/session/session_routing.hpp"
#include "lsw/agent/session/session_selector.hpp"
#include "lsw/agent/system/scale_steps.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/util/wide_string.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
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

std::vector<std::uint8_t> bytes_of(const std::string& s) {
    return {s.begin(), s.end()};
}

// ---- Base64 -------------------------------------------------------------
// Console output and window icons both ride the JSON channel base64
// encoded, so a padding bug here corrupts binary payloads in a way that
// only shows up as an unreadable icon.

void test_base64_vectors() {
    using lsw::agent::util::Base64;
    // RFC 4648 test vectors — all three padding cases.
    check(Base64::encode(bytes_of("")) == "", "empty encodes to empty");
    check(Base64::encode(bytes_of("f")) == "Zg==", "1 byte gets two pad chars");
    check(Base64::encode(bytes_of("fo")) == "Zm8=", "2 bytes get one pad char");
    check(Base64::encode(bytes_of("foo")) == "Zm9v", "3 bytes need no padding");
    check(Base64::encode(bytes_of("foob")) == "Zm9vYg==", "4 bytes");
    check(Base64::encode(bytes_of("fooba")) == "Zm9vYmE=", "5 bytes");
    check(Base64::encode(bytes_of("foobar")) == "Zm9vYmFy", "6 bytes");
}

void test_base64_binary() {
    using lsw::agent::util::Base64;
    // High bytes and NULs: console output is raw bytes, not text.
    const std::vector<std::uint8_t> data = {0x00, 0xFF, 0x80, 0x7F, 0x00};
    const std::string encoded = Base64::encode(data);
    check(encoded == "AP+AfwA=", "binary with NULs and high bytes encodes correctly");

    // Output length is always a multiple of 4.
    for (std::size_t n = 0; n < 32; ++n) {
        const std::vector<std::uint8_t> blob(n, 0xAB);
        check(Base64::encode(blob).size() % 4 == 0, "encoded length is a multiple of 4");
    }
}

// ---- URL → filename -----------------------------------------------------
// Used to name downloaded installers. A wrong answer here means the
// installer is saved under a name Windows then refuses to execute.

void test_url_file_name() {
    using lsw::agent::install::UrlFileName;
    check(UrlFileName::of(L"https://example.com/setup.exe") == L"setup.exe", "plain path");
    check(UrlFileName::of(L"https://example.com/a/b/c/tool.msi") == L"tool.msi", "nested path");
    check(UrlFileName::of(L"https://example.com/setup.exe?token=abc") == L"setup.exe",
          "query string is stripped");
    check(UrlFileName::of(L"https://example.com/setup.exe#frag") == L"setup.exe",
          "fragment is stripped");
    check(UrlFileName::of(L"https://example.com/setup.exe?a=1#b") == L"setup.exe",
          "query and fragment together");

    // Degenerate inputs must still yield a usable filename rather than an
    // empty one — an empty name would make the download path a directory.
    check(UrlFileName::of(L"https://example.com/") == L"download.bin", "trailing slash falls back");
    check(UrlFileName::of(L"") == L"download.bin", "empty url falls back");
    check(UrlFileName::of(L"https://example.com/?q=1") == L"download.bin",
          "query-only path falls back");
}

// ---- Job ids ------------------------------------------------------------
// The manager keys install progress by job id; a repeat would merge two
// steps' progress into one.

void test_job_ids_unique() {
    lsw::agent::install::JobIdGenerator ids;
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) {
        check(seen.insert(ids.next()).second, "job ids never repeat");
    }

    // Independent generators are independent: a test can own one without
    // being perturbed by another.
    lsw::agent::install::JobIdGenerator other;
    check(other.next() == lsw::agent::install::JobIdGenerator{}.next(),
          "a fresh generator starts from the same point");
}

// ---- Wide strings -------------------------------------------------------
// Every path comparison in the agent goes through these: the .lnk test
// that decides how an app is launched, the Uninstall-entry filter, the
// directory a launch is attributed to.

void test_wide_string_case() {
    using lsw::agent::util::WideString;
    check(WideString::lower(L"C:\\Program Files\\App.EXE") == L"c:\\program files\\app.exe",
          "lower folds ASCII");
    check(WideString::upper(L"Path") == L"PATH", "upper folds ASCII");
    check(WideString::lower(L"") == L"", "lower of empty is empty");
}

void test_wide_string_affixes() {
    using lsw::agent::util::WideString;
    check(WideString::iends_with(L"C:\\Start\\App.LNK", L".lnk"), "suffix ignores case");
    check(!WideString::iends_with(L"App.lnk.exe", L".lnk"), "suffix must be at the end");
    check(!WideString::iends_with(L"lnk", L".lnk"), "a needle longer than the haystack fails");
    check(WideString::iends_with(L"anything", L""), "an empty suffix matches");
    check(WideString::istarts_with(L"Microsoft Visual C++ 2015", L"microsoft visual c++"),
          "prefix ignores case");
    check(!WideString::istarts_with(L"My Microsoft", L"Microsoft"), "prefix must be at the start");
    check(WideString::istarts_with(L"KB", L"KB"), "a prefix equal to the string matches");
}

void test_wide_string_parent_dir() {
    using lsw::agent::util::WideString;
    check(WideString::parent_dir(L"C:\\Games\\th06.exe") == std::wstring{L"C:\\Games"},
          "parent of a backslash path");
    check(WideString::parent_dir(L"C:/Games/th06.exe") == std::wstring{L"C:/Games"},
          "forward slashes separate too");
    check(!WideString::parent_dir(L"th06.exe").has_value(), "a bare name has no parent");
    check(WideString::parent_dir(L"\\th06.exe") == std::wstring{},
          "a root-relative name has an empty parent, not none");
}

// ---- RPC params ---------------------------------------------------------
// The refusal wording is read by the host; it must not drift.

std::string refusal_of(void (*call)()) {
    try {
        call();
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return {};
}

void test_params_refusals() {
    using lsw::agent::rpc::Params;
    check(refusal_of([] { (void)Params{nlohmann::json::object(), "uia_state"}.require_hwnd(); }) ==
              "uia_state: missing hwnd",
          "absent hwnd is refused in the standard wording");
    check(refusal_of([] {
              (void)Params{nlohmann::json{{"hwnd", 0}}, "uia_state"}.require_hwnd();
          }) == "uia_state: missing hwnd",
          "a zero hwnd counts as missing");
    check(refusal_of([] {
              (void)Params{nlohmann::json::object(), "uia_subscribe_focus"}.require_pid();
          }) == "uia_subscribe_focus: missing pid",
          "absent pid is refused");
    check(refusal_of([] {
              (void)Params{nlohmann::json{{"url", ""}}, "download_install"}.require_string("url");
          }) == "download_install: missing url",
          "an empty string counts as missing");
    check(refusal_of([] {
              Params{nlohmann::json::object(), "uia_invoke"}.fail_missing("hwnd or selector");
          }) == "uia_invoke: missing hwnd or selector",
          "a combined requirement uses the same wording");
}

void test_params_values() {
    using lsw::agent::rpc::Params;
    const nlohmann::json json = {{"hwnd", 1234}, {"pid", 42}, {"path", "C:\\x"}};
    const Params p{json, "any"};
    check(p.require_hwnd() == 1234, "hwnd is read");
    check(p.require_pid() == 42, "pid is read");
    check(p.require_string("path") == "C:\\x", "a string is read");
    check(p.get("max_depth", 4) == 4, "an absent value falls back");
    check(Params{nlohmann::json::object(), "any"}.hwnd() == 0, "an absent hwnd reads as 0");
}


// ---- Session selection ---------------------------------------------------
// Which session a request without ``session`` lands in, which sessions
// get a helper, and whose "desktop ready" opens the install gate. A wrong
// answer launches an application on a desktop nobody is looking at, or
// declares the install finished over OOBE.

using lsw::agent::session::SessionInfo;
using lsw::agent::session::SessionSelector;
using lsw::agent::session::SessionState;

SessionInfo session(std::uint32_t id, const char* user, SessionState state, bool console) {
    SessionInfo s;
    s.id = id;
    s.user = user;
    s.state = state;
    s.console = console;
    return s;
}

void test_selector_default() {
    // The console user wins, whoever else is logged on.
    const SessionSelector a{{session(1, "Wizard", SessionState::Disconnected, false),
                             session(2, "bob", SessionState::Active, true)},
                            "wizard"};
    check(a.pick_default() == 2u, "the console session is the default");

    // No real user on the console: the wizard user's active session, then
    // any session of theirs, then any active one.
    const SessionSelector b{{session(1, "defaultuser0", SessionState::Active, true),
                             session(3, "bob", SessionState::Active, false),
                             session(4, "WIZARD", SessionState::Active, false)},
                            "wizard"};
    check(b.pick_default() == 4u, "the wizard user's active session, case-insensitively");
    const SessionSelector c{{session(3, "bob", SessionState::Active, false),
                             session(5, "wizard", SessionState::Disconnected, false)},
                            "wizard"};
    check(c.pick_default() == 5u, "a disconnected wizard session beats someone else's");
    const SessionSelector d{{session(3, "bob", SessionState::Disconnected, false),
                             session(6, "carol", SessionState::Active, false)},
                            ""};
    check(d.pick_default() == 6u, "any active session as the last resort");
    const SessionSelector e{{session(1, "defaultuser0", SessionState::Active, true)}, "wizard"};
    check(!e.pick_default().has_value(), "OOBE's account is nobody to launch for");
    const SessionSelector f{{}, "wizard"};
    check(!f.pick_default().has_value(), "no sessions, no default");
}

void test_selector_helpers_and_gate() {
    check(SessionSelector::is_setup_account("defaultuser0"), "defaultuser0 is OOBE's");
    check(SessionSelector::is_setup_account("DefaultUser100000"), "so is defaultuser100000");
    check(!SessionSelector::is_setup_account("alice"), "a real user is not");
    check(!SessionSelector::wants_helper(session(0, "SYSTEM", SessionState::Active, false)),
          "never a helper in session 0");
    check(!SessionSelector::wants_helper(session(1, "", SessionState::Connected, true)),
          "never a helper at the logon screen");
    check(SessionSelector::wants_helper(session(1, "alice", SessionState::Disconnected, false)),
          "a disconnected user's session keeps its helper");

    const SessionSelector sel{{session(1, "defaultuser0", SessionState::Active, true),
                               session(2, "wizard", SessionState::Active, false),
                               session(3, "bob", SessionState::Active, false)},
                              "wizard"};
    check(!sel.counts_for_gate(1), "OOBE's console session does not open the gate");
    check(sel.counts_for_gate(2), "the wizard user's session does");
    check(!sel.counts_for_gate(3), "someone else's remote session does not");
    check(!sel.counts_for_gate(9), "an unknown session does not");
    const SessionSelector console_only{{session(1, "alice", SessionState::Active, true)}, ""};
    check(console_only.counts_for_gate(1), "with the wizard unknown, the console counts");
}

// ---- Session routing -----------------------------------------------------

void test_routing_requested() {
    using lsw::agent::session::SessionRouting;
    check(!SessionRouting::requested(nlohmann::json::object(), "x").has_value(),
          "no session param means the default");
    check(SessionRouting::requested({{"session", 2}}, "x") == 2u, "an integer session is read");
    check(SessionRouting::requested({{"session", 0}}, "x") == 0u, "session 0 is explicit");
    check(!SessionRouting::requested({{"session", nullptr}}, "x").has_value(),
          "null is the same as absent");
    bool threw = false;
    try {
        (void)SessionRouting::requested({{"session", "2"}}, "launch");
    } catch (const std::invalid_argument& e) {
        threw = std::string{e.what()}.rfind("launch: ", 0) == 0;
    }
    check(threw, "a string session is refused, naming the command");
    threw = false;
    try {
        (void)SessionRouting::requested({{"session", -1}}, "x");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a negative session is refused");
    threw = false;
    try {
        (void)SessionRouting::requested({{"session", 1ULL << 40}}, "x");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a session past 32 bits is refused");

    check(SessionRouting::elevated(nlohmann::json::object()), "elevated by default");
    check(!SessionRouting::elevated({{"elevated", false}}), "elevated:false is honoured");
}

void test_routing_sub_ids() {
    using lsw::agent::session::SessionRouting;
    const std::uint64_t g = SessionRouting::global_sub_id(3, 7);
    check(SessionRouting::session_of_sub_id(g) == 3, "the session rides in the top half");
    check(SessionRouting::local_sub_id(g) == 7, "the helper's id in the bottom half");
    check(SessionRouting::global_sub_id(3, 0) == 0, "a failed subscription stays 0");
    check(g < (1ULL << 53), "small enough to survive a JSON double");

    // The reply on the way up, the unsubscribe on the way down.
    nlohmann::json reply = {{"ok", true}, {"sub_id", 7}};
    SessionRouting::globalize_reply(reply, 3);
    check(reply["sub_id"].get<std::uint64_t>() == g, "a reply's sub_id is globalised");
    const nlohmann::json unsubscribe = {{"sub_id", g}};
    check(SessionRouting::target(unsubscribe, "uia_unsubscribe") == 3u,
          "an unsubscribe finds its helper from the sub_id alone");
    const auto down = SessionRouting::params_for_helper(unsubscribe);
    check(down["sub_id"].get<std::uint64_t>() == 7, "and reaches it in the helper's numbering");

    const nlohmann::json both = {{"sub_id", g}, {"session", 5}};
    check(SessionRouting::target(both, "x") == 5u, "an explicit session beats the sub_id");
    check(!SessionRouting::params_for_helper(both).contains("session"),
          "the helper never sees the session param");
    check(!SessionRouting::target({{"sub_id", 7}}, "x").has_value(),
          "a sub_id with no session in it goes to the default session");
    nlohmann::json no_sub = {{"windows", nlohmann::json::array()}};
    SessionRouting::globalize_reply(no_sub, 3);
    check(!no_sub.contains("sub_id"), "replies without a sub_id are left alone");
}

// ---- The service <-> helper pipe ------------------------------------------

void test_pipe_codec() {
    using lsw::agent::session::PipeCodec;
    const nlohmann::json request = {{"id", 1}, {"cmd", "list_windows"}, {"params", {{"session", 1}}}};
    const nlohmann::json event = {{"event", "uia_window_opened"}, {"hwnd", 42}};
    const std::string bytes = PipeCodec::encode(request) + PipeCodec::encode(event);
    check(bytes.size() > 10 && bytes[0] == 1, "a control envelope, type 1 first");

    // Fed one byte at a time: a pipe read can split anywhere.
    PipeCodec codec;
    std::vector<nlohmann::json> out;
    for (const char c : bytes) {
        const auto b = static_cast<std::uint8_t>(c);
        codec.feed(&b, 1);
        check(codec.drain(out), "a partial frame is not an error");
    }
    check(out.size() == 2, "both messages reassemble");
    check(out[0] == request && out[1] == event, "byte for byte");
    check(PipeCodec::is_request(out[0]) && !PipeCodec::is_event(out[0]), "a request is a request");
    check(PipeCodec::is_event(out[1]) && !PipeCodec::is_reply(out[1]), "an event is an event");
    check(PipeCodec::is_reply({{"id", 1}, {"result", {}}}), "a result is a reply");
    check(PipeCodec::is_reply({{"id", 1}, {"error", "x"}}), "an error is a reply");

    // A frame whose JSON does not parse is skipped, not fatal.
    PipeCodec bad;
    const std::uint8_t garbage[] = {1, 0, 0, 0, 3, '{', '{', '{'};
    bad.feed(garbage, sizeof(garbage));
    const std::string good = PipeCodec::encode(event);
    bad.feed(reinterpret_cast<const std::uint8_t*>(good.data()), good.size());
    std::vector<nlohmann::json> after;
    check(bad.drain(after) && after.size() == 1 && bad.malformed() == 1,
          "malformed JSON is skipped and the stream goes on");

    // A length past the cap ends the stream once that much is buffered.
    PipeCodec huge;
    const std::uint8_t header[] = {1, 0x7F, 0xFF, 0xFF, 0xFF};
    huge.feed(header, sizeof(header));
    std::vector<nlohmann::json> none;
    check(huge.drain(none), "a big header alone is still just a partial frame");
    const std::vector<std::uint8_t> blob(PipeCodec::kMaxMessageBytes + 1, 'x');
    huge.feed(blob.data(), blob.size());
    check(!huge.drain(none), "but past the cap the stream is declared corrupt");
}

void test_pending_calls() {
    using lsw::agent::session::PendingCalls;
    using namespace std::chrono_literals;
    PendingCalls calls;
    const auto a = calls.open();
    const auto b = calls.open();
    check(a != b, "ids are unique");
    check(calls.complete(a, {{"id", a}, {"result", 1}}), "a reply is delivered");
    const auto got = calls.wait(a, 10ms);
    check(got.has_value() && (*got)["result"] == 1, "a reply that came first is still there");
    check(!calls.complete(a, nlohmann::json::object()), "a second reply is dropped");

    check(!calls.wait(b, 10ms).has_value(), "no reply is a timeout");
    check(!calls.complete(b, nlohmann::json::object()), "a late reply after a timeout is dropped");
    check(calls.in_flight() == 0, "nothing is left behind");

    // A waiter blocked on a helper that goes away is woken with the
    // reason, long before its timeout.
    const auto c = calls.open();
    std::thread closer{[&] {
        std::this_thread::sleep_for(20ms);
        calls.close("the session helper in session 1 went away");
    }};
    const auto started = std::chrono::steady_clock::now();
    bool woke = false;
    try {
        (void)calls.wait(c, 10s);
    } catch (const std::runtime_error& e) {
        woke = std::string{e.what()}.find("went away") != std::string::npos;
    }
    closer.join();
    check(woke, "closing fails the waiter with the reason");
    check(std::chrono::steady_clock::now() - started < 5s, "and does so promptly");
    bool refused = false;
    try {
        (void)calls.open();
    } catch (const std::runtime_error&) {
        refused = true;
    }
    check(refused, "nothing new is sent on a closed channel");
}

// ---- Command line -----------------------------------------------------------

void test_command_line() {
    using lsw::agent::core::CommandLine;
    check(CommandLine::parse({}).mode == CommandLine::Mode::Foreground,
          "no arguments is the foreground");
    check(CommandLine::parse({L"--service"}).mode == CommandLine::Mode::Service, "--service");
    const auto helper = CommandLine::parse({L"--session", L"--pipe", L"\\\\.\\pipe\\x"});
    check(helper.mode == CommandLine::Mode::SessionHelper && helper.pipe == L"\\\\.\\pipe\\x" &&
              helper.error.empty(),
          "--session --pipe NAME");
    check(!CommandLine::parse({L"--session"}).error.empty(), "a helper needs its pipe");
    check(!CommandLine::parse({L"--pipe"}).error.empty(), "--pipe needs a name");
    check(!CommandLine::parse({L"--service", L"--pipe", L"x"}).error.empty(),
          "--pipe is for helpers only");
    check(!CommandLine::parse({L"--service", L"--session"}).error.empty(), "one mode at a time");
    check(!CommandLine::parse({L"--srevice"}).error.empty(), "a typo is an error, not the foreground");
}

void test_scale_steps() {
    using lsw::agent::system::ScaleSteps;
    // A display recommended at 100 % that goes up to 200 %.
    const ScaleSteps at_100{0, 4};
    check(at_100.relative_for(120) == 1 && at_100.percent_at(1) == 125,
          "1.2 is the 125 % step, one above a 100 % recommendation");
    check(at_100.relative_for(100) == 0, "the recommended scale is no step at all");
    check(at_100.relative_for(112) == 0 && at_100.relative_for(113) == 1,
          "each percentage goes to the nearer step");
    check(at_100.relative_for(500) == 4 && at_100.percent_at(4) == 200,
          "past the display's largest step is that step");
    check(at_100.relative_for(50) == 0, "below 100 % is 100 %");

    // Recommended at 150 %: steps are counted from there.
    const ScaleSteps at_150{2, 2};
    check(at_150.relative_for(120) == -1 && at_150.percent_at(-1) == 125,
          "below the recommendation is a negative step");
    check(at_150.relative_for(200) == 2, "up to the display's largest step");

    // Halfway between 250 and 300 % goes to the lower.
    const ScaleSteps wide{0, 11};
    check(wide.relative_for(275) == 6 && wide.percent_at(6) == 250, "a tie goes down");
    check(wide.percent_at(40) == 500 && wide.percent_at(-3) == 100,
          "a step outside the display's range is its nearest end");
}

}  // namespace

int main() {
    test_base64_vectors();
    test_base64_binary();
    test_url_file_name();
    test_job_ids_unique();
    test_wide_string_case();
    test_wide_string_affixes();
    test_wide_string_parent_dir();
    test_params_refusals();
    test_params_values();
    test_selector_default();
    test_selector_helpers_and_gate();
    test_routing_requested();
    test_routing_sub_ids();
    test_pipe_codec();
    test_pending_calls();
    test_command_line();
    test_scale_steps();
    std::printf("agent logic: %d checks passed\n", g_checks);
    return 0;
}
