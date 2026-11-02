#include "lsw/host/services/update_batch.hpp"

#include <cstdio>
#include <string>

using lsw::host::services::GuestBundle;
using lsw::host::services::UpdateBatch;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

size_t count(const std::string& text, const std::string& part) {
    size_t n = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) {
        ++n;
    }
    return n;
}

GuestBundle::Contents contents() {
    GuestBundle::Contents c;
    c.version = "0.2.0";
    c.files.push_back({"C:\\ProgramData\\LSW\\lsw-agent.exe", {}, "h1"});
    c.files.push_back({"C:\\av\\package\\avgpu.sys", {}, "h2"});
    c.files.push_back({"C:\\ProgramData\\LSW\\lsw-guest-update.ps1", {}, "h3"});
    return c;
}

}

int main() {
    const std::string release = "0123456789abcdef0123456789abcdef";
    const GuestBundle::Contents c = contents();
    const std::string plain = UpdateBatch::send(5555, release, c, false);
    const std::string forced = UpdateBatch::send(5555, release, c, true);

    check(has(plain, "set STAGE=C:\\ProgramData\\LSW\\update\\0123456789abcdef\r\n"),
          "the stage is the update directory under the release's first 16 characters");
    check(has(plain, "-o \"%STAGE%\\C\\ProgramData\\LSW\\lsw-agent.exe\" http://10.0.2.2:5555/f0 ")
              && has(plain, "-o \"%STAGE%\\C\\av\\package\\avgpu.sys\" http://10.0.2.2:5555/f1 ")
              && has(plain, "-o \"%STAGE%\\C\\ProgramData\\LSW\\lsw-guest-update.ps1\" http://10.0.2.2:5555/f2 "),
          "every file is fetched by its index into <stage>\\<staged path>");
    check(has(plain, "-o \"%STAGE%\\manifest.txt\" http://10.0.2.2:5555/manifest.txt ")
              && has(plain, "-o \"%STAGE%\\shares.txt\" http://10.0.2.2:5555/shares.txt "),
          "manifest.txt and shares.txt are fetched into the stage's top");
    check(count(plain, "curl.exe") == 5, "one download a file and the two lists, no more");
    check(count(plain, "exit /b 8)") == 5, "every download that fails exits 8");
    check(!has(plain, "avgpu.sys\" >>") && !has(plain, "download of avgpu"),
          "the log lines carry the file's index, not its path");
    check(has(plain, "-Stage \"%STAGE%\" -Release " + release + " -Version 0.2.0"),
          "run.cmd is written with -Stage, -Release and -Version");
    check(has(plain, "lsw-guest-update.ps1\" -Stage"), "run.cmd runs the update script from the stage");
    check(!has(plain, "-Force"), "an unforced run has no -Force");
    check(has(forced, "-Version 0.2.0 -Force\r\n"), "a forced run ends run.cmd's line with -Force");
    check(has(plain, "schtasks /Create /TN lsw-update") && has(plain, "/RU SYSTEM") && has(plain, "schtasks /Run /TN lsw-update"),
          "the one-shot task is registered as SYSTEM and started");
    check(has(plain, "exit /b 9)") && count(plain, "exit /b 9)") == 2, "creating or starting the task failing exits 9");
    check(plain.rfind("exit /b 0\r\n") == plain.size() - std::string{"exit /b 0\r\n"}.size(), "it ends exit 0");

    const auto query = plain.find("schtasks /Query /TN lsw-update");
    const auto remove = plain.find("rd /s /q");
    const auto first_fetch = plain.find("curl.exe");
    check(query != std::string::npos && remove != std::string::npos && query < remove && remove < first_fetch,
          "the task is asked about before the stage is removed, and both before any download");
    check(has(plain, "findstr /i /c:Running >nul && exit /b 10"), "a Running task exits 10 before the stage is cleared");

    check(UpdateBatch::send(1, release, c, false) != UpdateBatch::send(2, release, c, false),
          "the port is in the script");
    check(has(UpdateBatch::send(1, release, GuestBundle::Contents{}, false), "manifest.txt"),
          "a bundle with no files still fetches the lists");
    check(plain.find('\n') != std::string::npos && count(plain, "\r\n") == count(plain, "\n"),
          "every line ends CRLF");

    const std::string read = UpdateBatch::read_result("2026-11-02T10:00:00.000Z");
    check(has(read, "if not exist \"C:\\ProgramData\\LSW\\update-result.json\" exit /b 20"), "no result file is 20");
    check(has(read, "findstr /c:\"2026-11-02T10:00:00.000Z\" \"C:\\ProgramData\\LSW\\update-result.json\" >nul && exit /b 20"),
          "a result still holding the stamp from before the run is 20");
    check(has(read, "ok\\\":false") && has(read, "&& exit /b 22"), "a failed result is 22");
    check(read.rfind("exit /b 23\r\n") == read.size() - std::string{"exit /b 23\r\n"}.size(), "an ok result of this run is 23");
    check(!has(UpdateBatch::read_result(""), "findstr /c:\"\""), "with no stamp before the run there is nothing to compare");

    const QString log = QStringLiteral("C:\\ProgramData\\LSW\\logs\\update.log");
    const auto fail = [](bool ok, int code, const char* msg = "") { return UpdateBatch::send_failure(ok, code, msg); };
    check(fail(true, 8) && fail(true, 8)->contains(QStringLiteral("could not be copied")) && fail(true, 8)->contains(log),
          "exit 8 is the files not copied, with the log's place");
    check(fail(true, 9) && fail(true, 9)->contains(QStringLiteral("would not start the update")) && fail(true, 9)->contains(log),
          "exit 9 is the task not started, with the log's place");
    check(fail(true, 10) && *fail(true, 10) == QStringLiteral("An update is still running in Windows."),
          "exit 10 is an update still running, without the log");
    check(fail(true, 3, "boom") && fail(true, 3, "boom")->contains(QStringLiteral("Starting the update failed: boom")),
          "any other exit is a failed start with the agent's message");
    check(fail(false, 0, "agent said no") && fail(false, 0, "agent said no")->contains(QStringLiteral("agent said no")),
          "a job that ended not ok is a failure even with exit 0");
    check(!fail(true, 0), "exit 0 with ok is no failure");
    check(UpdateBatch::unreported(22).contains(QStringLiteral("could not be updated")),
          "result exit 22 says the update failed");
    check(UpdateBatch::unreported(23).contains(QStringLiteral("does not report the new release")),
          "result exit 23 says the agent does not report the new release");
    check(UpdateBatch::unreported(20).contains(QStringLiteral("did not report the new release in 15 minutes"))
              && UpdateBatch::unreported(std::nullopt).contains(QStringLiteral("previous LSW agent"))
              && UpdateBatch::unreported(20).contains(log),
          "result exit 20, or none, says nothing came in 15 minutes");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
