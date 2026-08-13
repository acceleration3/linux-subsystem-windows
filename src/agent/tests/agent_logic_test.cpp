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

#include "lsw/agent/install/job_id_generator.hpp"
#include "lsw/agent/install/url_file_name.hpp"
#include "lsw/agent/util/base64.hpp"

#include <cstdint>
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
    check(UrlFileName::of(L"https://example.com/setup.exe") == L"setup.exe",
          "plain path");
    check(UrlFileName::of(L"https://example.com/a/b/c/tool.msi") == L"tool.msi",
          "nested path");
    check(UrlFileName::of(L"https://example.com/setup.exe?token=abc") == L"setup.exe",
          "query string is stripped");
    check(UrlFileName::of(L"https://example.com/setup.exe#frag") == L"setup.exe",
          "fragment is stripped");
    check(UrlFileName::of(L"https://example.com/setup.exe?a=1#b") == L"setup.exe",
          "query and fragment together");

    // Degenerate inputs must still yield a usable filename rather than an
    // empty one — an empty name would make the download path a directory.
    check(UrlFileName::of(L"https://example.com/") == L"download.bin",
          "trailing slash falls back");
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

}  // namespace

int main() {
    test_base64_vectors();
    test_base64_binary();
    test_url_file_name();
    test_job_ids_unique();
    std::printf("agent logic: %d checks passed\n", g_checks);
    return 0;
}
