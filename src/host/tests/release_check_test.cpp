#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/release_check.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using lsw::host::services::ReleaseCheck;
using lsw::host::services::ReleaseStatus;
using lsw::host::services::StatusHub;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

QJsonObject release(const char* tag, const char* body, bool draft = false, bool prerelease = false) {
    return QJsonObject{
        {QStringLiteral("tag_name"), QString::fromLatin1(tag)},
        {QStringLiteral("html_url"),
         QStringLiteral("https://github.com/o/r/releases/tag/%1").arg(QString::fromLatin1(tag))},
        {QStringLiteral("published_at"), QStringLiteral("2026-11-19T20:18:47Z")},
        {QStringLiteral("body"), QString::fromUtf8(body)},
        {QStringLiteral("draft"), draft},
        {QStringLiteral("prerelease"), prerelease},
    };
}

QByteArray page(const QJsonArray& releases) {
    return QJsonDocument(releases).toJson();
}

class NoProbe : public lsw::host::vm::IGuestStatsProbe {
public:
    bool available() const override { return false; }
    lsw::host::vm::GuestStatsSample sample() override { return {}; }
};

}

int main(int argc, char** argv) {
    QApplication app{argc, argv};

    {
        const QStringList lines = ReleaseCheck::headlines(QStringLiteral(
            "## Fixes\n\n- **Opening a program from the command line runs it** ([#33](x)). More.\n"
            "- **The manager reconnects when its connection to Windows dies.** It now notices.\n"
            "* **`lsw --version` prints the version**\n"
            "- **Window mirroring:**\n  - nested detail\n"
            "Plain **bold** in prose is not a headline.\n- a bullet without bold\n"));
        check(lines.size() == 4, "every bold bullet lead is a headline and nothing else is");
        check(lines.value(0) == QStringLiteral("Opening a program from the command line runs it"),
              "a headline is the bold text alone");
        check(lines.value(1) == QStringLiteral("The manager reconnects when its connection to Windows dies"),
              "a headline drops its closing full stop");
        check(lines.value(2) == QStringLiteral("lsw --version prints the version"),
              "a headline drops code marks");
        check(lines.value(3) == QStringLiteral("Window mirroring"), "a group lead drops its colon");
    }

    {
        QString error;
        const auto newer = ReleaseCheck::newer_than(
            page({release("v0.3.3", "- **Three three**"), release("v0.3.10", "- **Three ten**"),
                  release("v0.3.4", "- **Three four**"), release("v0.4.0", "- **Draft**", true),
                  release("v0.5.0", "- **Pre**", false, true), release("v0.3.2", "- **Old**"),
                  release("nightly", "- **Not a version**")}),
            QStringLiteral("0.3.3"), &error);
        check(newer.has_value(), "a list of releases is read");
        check(newer && newer->size() == 2, "only published releases newer than the running one count");
        check(newer && newer->value(0).version == QStringLiteral("0.3.10") &&
                  newer->value(1).version == QStringLiteral("0.3.4"),
              "newer releases are ordered newest first by number, not by text");
        check(newer && newer->value(1).headlines == QStringList{QStringLiteral("Three four")},
              "each release carries its own headlines");
        check(newer && newer->value(0).page.toString() == QStringLiteral("https://github.com/o/r/releases/tag/v0.3.10"),
              "each release links to its page");
    }

    {
        QString error;
        const auto newer = ReleaseCheck::newer_than(page({release("v0.3.3", "")}), QStringLiteral("0.3.3"), &error);
        check(newer && newer->isEmpty(), "the running release itself is not newer");
        const auto ahead = ReleaseCheck::newer_than(page({release("v0.3.3", "")}), QStringLiteral("0.4.0"), &error);
        check(ahead && ahead->isEmpty(), "a build ahead of every release has nothing newer");
    }

    {
        QString error;
        check(!ReleaseCheck::newer_than("{\"message\":\"API rate limit exceeded\"}", QStringLiteral("0.3.3"), &error) &&
                  !error.isEmpty(),
              "an answer that is not a list is an error, not an empty list");
        error.clear();
        check(!ReleaseCheck::newer_than("not json", QStringLiteral("0.3.3"), &error) && !error.isEmpty(),
              "an unreadable answer is an error");
        error.clear();
        check(!ReleaseCheck::newer_than(page({release("v0.3.4", "")}), QStringLiteral("unknown"), &error) &&
                  !error.isEmpty(),
              "a build without a version cannot claim anything is newer");
    }

    {
        lsw::host::link::AgentRpcClient rpc;
        lsw::host::config::Settings settings;
        lsw::host::config::ManagerSettings manager_settings;
        lsw::host::vm::LibvirtConnection libvirt;
        lsw::host::vm::Lifecycle lifecycle{libvirt};
        NoProbe probe;
        StatusHub hub{libvirt, lifecycle, rpc, probe, settings, manager_settings};
        int asked = 0;
        ReleaseCheck::Done pending;
        ReleaseCheck release_check{hub, QStringLiteral("0.3.3"),
                                   [&](const QUrl&, ReleaseCheck::Done done) {
                                       ++asked;
                                       pending = std::move(done);
                                   }};

        check(hub.release().state == ReleaseStatus::State::Unchecked, "nothing is known before a check");
        release_check.check();
        release_check.check();
        check(asked == 1 && hub.release().state == ReleaseStatus::State::Checking,
              "a check asks once and shows that it is checking until GitHub answers");
        pending(page({release("v0.3.4", "- **Three four**"), release("v0.3.3", "")}), {});
        check(hub.release().state == ReleaseStatus::State::Newer && hub.release().newer.size() == 1 &&
                  hub.release().newer.first().version == QStringLiteral("0.3.4") && !hub.release().checked.isEmpty(),
              "the answer reaches the status the pages read, with the newer release and when it was checked");

        release_check.check();
        pending({}, QStringLiteral("Host api.github.com not found"));
        check(hub.release().state == ReleaseStatus::State::Failed &&
                  hub.release().error.contains(QStringLiteral("not found")),
              "a failed request is shown as a failed check with its reason");

        release_check.check();
        pending(page({release("v0.3.3", "")}), {});
        check(hub.release().state == ReleaseStatus::State::Latest && hub.release().newer.isEmpty(),
              "running the newest release shows as the latest");
        check(!lsw::host::config::ManagerSettings::default_value(
                   lsw::host::config::ManagerSettings::Key::kCheckUpdates).toBool(),
              "the manager does not ask GitHub on startup unless the setting is turned on");
    }

    check(ReleaseCheck::releases_url().host() == QStringLiteral("api.github.com"),
          "the check asks GitHub's release list");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
