#include "lsw/agent/apps/launch_command.hpp"
#include "lsw/agent/apps/upload_store.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/linkproto/app_package.hpp"
#include "lsw/linkproto/file_transfer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

template<class F>
void rejects(F&& action, const char* label) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    check(false, label);
}

void base64() {
    using lsw::agent::util::Base64;
    check(Base64::decode("").empty(), "empty base64");
    for (std::size_t n = 0; n < 1024; ++n) {
        std::vector<std::uint8_t> bytes(n);
        for (std::size_t i = 0; i < n; ++i)
            bytes[i] = static_cast<std::uint8_t>(i);
        check(Base64::decode(Base64::encode(bytes)) == bytes,
              "binary roundtrip, all padding cases");
    }
    for (const auto* bad :
         {"x", "====", "!AAA", "AA A", "AA=A", "AA==AAAA", "AB==", "AAB=", "Zg=\n"})
        rejects([&] { (void)Base64::decode(bad); }, "invalid base64 rejected");
}

void commands() {
    for (const auto* package :
         {"C:\\setup.msix", "C:\\setup.MSIXBUNDLE", "C:\\setup.appx", "C:\\setup.APPXBUNDLE"})
        check(lsw::linkproto::is_app_package(package),
              "App Installer package extensions recognized");
    for (const auto* executable :
         {"", "C:\\setup.msi", "C:\\msix.exe", "C:\\setup.msix.exe", "C:\\setup.msix\\app.exe"})
        check(!lsw::linkproto::is_app_package(executable), "ordinary executable is not a package");
    using lsw::agent::apps::LaunchCommand;
    const std::wstring windows = L"C:\\Windows";
    const auto exe = LaunchCommand(L"C:\\a b\\app.exe", windows, L"--flag");
    check(exe.application == L"C:\\a b\\app.exe" &&
              exe.command_line == L"\"C:\\a b\\app.exe\" --flag",
          "ordinary executable unchanged");
    const auto msi = LaunchCommand(L"C:\\a b\\setup.MSI", windows, L"PROPERTY=value");
    check(
        msi.application == windows + L"\\System32\\msiexec.exe" &&
            msi.command_line ==
                L"\"C:\\Windows\\System32\\msiexec.exe\" /i \"C:\\a b\\setup.MSI\" PROPERTY=value",
        "MSI launched interactively, case-insensitive, quoted with arguments");
    check(LaunchCommand(L"C:\\update.msp", windows, L"").command_line.find(L" /p ") !=
              std::wstring::npos,
          "MSP uses patch mode");
    for (const auto* suffix : {L"bat", L"cmd"}) {
        const auto cmd =
            LaunchCommand(std::wstring(L"C:\\a & b\\run.") + suffix, windows, L"one two");
        check(cmd.application.ends_with(L"cmd.exe") &&
                  cmd.command_line.find(L" /d /s /c \"\"C:\\a & b\\run.") != std::wstring::npos &&
                  cmd.command_line.ends_with(L"\" one two\""),
              "batch command has nested cmd quoting");
    }
    const auto ps = LaunchCommand(L"C:\\script.ps1", windows, L"-Name test");
    check(ps.application.ends_with(L"powershell.exe") &&
              ps.command_line.ends_with(L"-File \"C:\\script.ps1\" -Name test"),
          "PowerShell uses File not Command");
    for (const auto* suffix : {L"vbs", L"js", L"wsf"})
        check(LaunchCommand(std::wstring(L"C:\\script.") + suffix, windows, L"")
                  .application.ends_with(L"wscript.exe"),
              "Windows scripts use wscript");
}

void uploads(const std::filesystem::path& root) {
    using lsw::agent::apps::UploadStore;
    std::filesystem::path completed;
    {
        UploadStore store(root);
        for (const auto* bad :
             {"", "../evil.exe", "..\\evil.exe", "C:\\evil.exe", "evil.exe:stream", "CON.exe",
              "nul", "COM1.exe", "LPT9", "CON .exe", "bad.", "bad ", "a%PATH%.exe", "a!x!.exe"})
            rejects([&] { (void)store.begin(bad, 1); }, "unsafe Windows name rejected");
        rejects([&] { (void)store.begin("a.exe", 0); }, "empty file rejected");
        rejects([&] { (void)store.begin("a.exe", lsw::linkproto::kMaxUploadBytes + 1); },
                "oversize file rejected");
        const std::vector<std::uint8_t> first{0, 255, 1};
        const std::vector<std::uint8_t> last{128, 42};
        const auto id = store.begin("installer space.msi", 5);
        const auto duplicate = store.begin("installer space.msi", 5);
        check(id != duplicate, "same filename receives unique directory");
        rejects([&] { (void)store.finish(id); }, "incomplete file cannot finish");
        rejects([&] { (void)store.append(id, 1, first); }, "out-of-order chunk rejected");
        check(store.append(id, 0, first) == 3, "first offset acknowledged");
        rejects([&] { (void)store.append(id, 0, first); }, "duplicate chunk rejected");
        rejects([&] { (void)store.append(id, 3, first); }, "declared size enforced");
        rejects([&] { (void)store.append(id, 3, {}); }, "empty chunk rejected");
        check(store.append(id, 3, last) == 5, "second chunk appended");
        completed = store.finish(id);
        check(completed.filename() == "installer space.msi", "original filename preserved");
        std::ifstream file(completed, std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(file), {}};
        check(content == std::string("\0\xff\x01\x80\x2a", 5), "binary file written exactly");
        rejects([&] { (void)store.finish(id); }, "completed upload ID invalidated");
        store.abort(id);
        check(std::filesystem::exists(completed), "abort cannot delete completed files");
        store.abort(duplicate);
        check(!std::filesystem::exists(root / duplicate),
              "cancel removes only its partial transfer");
        store.abort(duplicate);
        const auto big = store.begin("big.exe", lsw::linkproto::kFileChunkBytes + 1);
        const std::vector<std::uint8_t> oversized(lsw::linkproto::kFileChunkBytes + 1);
        rejects([&] { (void)store.append(big, 0, oversized); }, "chunk limit enforced");
        const auto unicode = store.begin("caf\xc3\xa9.exe", 1);
        check(!unicode.empty(), "UTF-8 filename accepted");
    }
    check(std::filesystem::exists(completed), "completed upload survives service teardown");
    check(std::distance(std::filesystem::directory_iterator(root),
                        std::filesystem::directory_iterator{}) == 1,
          "service teardown cleans unfinished uploads");
}
}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lsw-upload-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    base64();
    commands();
    uploads(root);
    // Only the unique test-owned directory is removed.
    std::filesystem::remove_all(root);
    std::puts("upload and launch command tests passed");
}
