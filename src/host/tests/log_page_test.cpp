#include "lsw/base/log_format.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/log_panel.hpp"
#include "lsw/host/services/log_collector.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>

#include <cstdio>

using lsw::host::apps::LogRecord;
using lsw::host::apps::LogStore;
using lsw::host::manager::LogPanel;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void pump(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

LogRecord record(const char* source, const char* level, const QString& msg, int ts) {
    LogRecord r;
    r.source = QString::fromLatin1(source);
    r.level = QString::fromLatin1(level);
    r.msg = msg;
    r.ts_ms = static_cast<quint64>(ts);
    return r;
}

int blocks(LogPanel& panel) {
    return panel.view()->document()->isEmpty() ? 0 : panel.view()->document()->blockCount();
}

QString text(LogPanel& panel) {
    return panel.view()->toPlainText();
}

QString file_line(int n) {
    std::string line;
    lsw::base::LogFormat::append_line(line, 1'700'000'000'000 + n * 1000, 2, "x",
                                      "line " + std::to_string(n));
    return QString::fromStdString(line).trimmed();
}

void write_lines(const QString& path, int from, int to, QIODevice::OpenMode mode) {
    QFile file{path};
    (void)file.open(mode);
    for (int n = from; n <= to; ++n) {
        file.write((file_line(n) + QLatin1Char('\n')).toUtf8());
    }
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    lsw::host::config::Settings settings;
    (void)settings.ensure_directories();
    lsw::host::config::ManagerSettings manager_settings;
    lsw::host::link::AgentRpcClient rpc;
    lsw::host::services::LogCollector collector{
        settings, manager_settings, rpc, [] { return lsw::host::services::SystemReport::Inputs{}; }};
    LogStore store;
    LogPanel panel{store, settings, collector};
    panel.resize(700, 400);

    check(panel.windowTitle() == QStringLiteral("Log"), "the page is titled Log");
    check(panel.view()->lineWrapMode() == QPlainTextEdit::WidgetWidth, "lines wrap at the widget width");
    check(panel.view()->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
          "the horizontal scroll bar is always off");

    int last_count = -1;
    QObject::connect(&panel, &LogPanel::record_count_changed, [&](int n) { last_count = n; });

    store.post(record("manager", "INFO", QStringLiteral("one"), 1000));
    store.post(record("agent", "WARN", QStringLiteral("two"), 2000));
    store.post(record("qt", "ERROR", QStringLiteral("three"), 3000));
    pump(200);
    check(blocks(panel) == 0 && text(panel).isEmpty(), "hidden: records are not drawn");
    check(panel.shown() == 3 && last_count == 3, "hidden: the count still follows");
    check(store.records().size() == 3, "hidden: the store keeps them");

    panel.show();
    pump(50);
    check(blocks(panel) == 3 && text(panel).contains(QStringLiteral("one")) &&
              text(panel).contains(QStringLiteral("three")),
          "shown: what arrived meanwhile is drawn");
    check(panel.shown() == 3, "shown: the count is the same, not doubled");

    store.post(record("manager", "INFO", QStringLiteral("four"), 4000));
    pump(200);
    check(blocks(panel) == 4 && panel.shown() == 4, "visible: a batch is drawn as it arrives");

    panel.hide();
    store.post(record("manager", "INFO", QStringLiteral("five"), 5000));
    store.post(record("agent", "INFO", QStringLiteral("six"), 6000));
    pump(200);
    check(blocks(panel) == 4, "hidden again: nothing more is drawn");
    panel.show();
    pump(50);
    check(blocks(panel) == 6 && panel.shown() == 6, "shown again: only the missing two are added");
    const QString all = text(panel);
    check(all.indexOf(QStringLiteral("five")) < all.indexOf(QStringLiteral("six")) &&
              all.count(QStringLiteral("five")) == 1,
          "shown again: no record is drawn twice");

    check(panel.view()->horizontalScrollBar()->isVisible() == false,
          "no horizontal scroll bar with the lines at hand");
    store.post(record("manager", "INFO", QString(600, QLatin1Char('w')) + QStringLiteral(" tail"), 7000));
    pump(200);
    check(!panel.view()->horizontalScrollBar()->isVisible() &&
              panel.view()->horizontalScrollBar()->maximum() == 0,
          "a very long line wraps and does not scroll sideways");

    panel.select_source(LogPanel::store_key(QStringLiteral("manager")));
    pump(20);
    check(text(panel).contains(QStringLiteral("one")) && !text(panel).contains(QStringLiteral("two")) &&
              !text(panel).contains(QStringLiteral("six")),
          "a source shows only its own records");
    panel.select_source(QStringLiteral("all"));

    store.post(record("agent session 2", "INFO", QStringLiteral("sess"), 8000));
    store.post(record("mirror notepad", "INFO", QStringLiteral("mir"), 8100));
    pump(200);
    check(panel.picker()->findText(QStringLiteral("Agent session 2")) >= 0 &&
              panel.picker()->findText(QStringLiteral("Mirror notepad")) >= 0,
          "sources seen in the store appear in the picker");
    panel.select_source(LogPanel::store_key(QStringLiteral("agent")));
    pump(20);
    check(text(panel).contains(QStringLiteral("two")) && text(panel).contains(QStringLiteral("sess")) &&
              !text(panel).contains(QStringLiteral("mir")),
          "Agent shows its sessions too");
    panel.select_source(LogPanel::store_key(QStringLiteral("agent session 2")));
    pump(20);
    check(text(panel).contains(QStringLiteral("sess")) && !text(panel).contains(QStringLiteral("two")),
          "one agent session shows alone");
    panel.select_source(QStringLiteral("all"));
    pump(20);

    store.post(record("agent", "INFO", QStringLiteral("late-but-early"), 500));
    pump(200);
    check(text(panel).startsWith(QStringLiteral("00:")) || text(panel).indexOf(QStringLiteral("late-but-early")) <
                                                             text(panel).indexOf(QStringLiteral("one")),
          "All merges by time: a record older than the page rebuilds into its place");

    store.clear();
    pump(50);
    panel.hide();
    store.post(record("agent", "INFO", QStringLiteral("b-agent"), 20000));
    store.post(record("manager", "INFO", QStringLiteral("a-manager"), 10000));
    store.post(record("mirror x", "INFO", QStringLiteral("c-mirror"), 30000));
    pump(200);
    panel.show();
    pump(50);
    const QString merged = text(panel);
    check(merged.indexOf(QStringLiteral("a-manager")) < merged.indexOf(QStringLiteral("b-agent")) &&
              merged.indexOf(QStringLiteral("b-agent")) < merged.indexOf(QStringLiteral("c-mirror")),
          "All merges the sources in time order");

    for (auto* button : panel.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Info")) {
            button->click();
        }
    }
    pump(20);
    check(blocks(panel) == 0, "turning Info off hides the info records");
    for (auto* button : panel.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Info")) {
            button->click();
        }
    }
    pump(20);
    check(blocks(panel) == 3, "and turning it on shows them again");

    store.clear();
    pump(50);
    panel.hide();
    for (int i = 0; i < 2500; ++i) {
        store.post(record("manager", "INFO", QStringLiteral("bulk %1").arg(i), 100000 + i));
    }
    pump(400);
    check(blocks(panel) == 0, "hidden: a flood draws nothing");
    panel.show();
    pump(100);
    check(blocks(panel) == 2500 && panel.shown() == 2500, "shown: a flood is rebuilt in full");

    QDir{}.mkpath(settings.vm_logs_dir());
    const QString gpu_log = settings.vm_avgpu_log_path();
    QFile::remove(gpu_log);
    write_lines(gpu_log, 1, 3000, QIODevice::WriteOnly);
    panel.select_source(LogPanel::file_key(gpu_log));
    pump(100);
    check(blocks(panel) == LogPanel::kRebuildOver && text(panel).contains(QStringLiteral("line 3000")) &&
              !text(panel).contains(QStringLiteral("line 1000")) &&
              text(panel).contains(QStringLiteral("line 1001")),
          "a file source starts with its last 2000 lines");
    check(panel.shown() == 2000, "the count is the file's lines");

    write_lines(gpu_log, 3001, 3005, QIODevice::WriteOnly | QIODevice::Append);
    pump(900);
    check(text(panel).contains(QStringLiteral("line 3005")), "a file source follows growth");

    panel.hide();
    write_lines(gpu_log, 3006, 3010, QIODevice::WriteOnly | QIODevice::Append);
    pump(900);
    check(!text(panel).contains(QStringLiteral("line 3010")), "hidden: the file is not read");
    panel.show();
    pump(900);
    check(text(panel).contains(QStringLiteral("line 3010")), "shown again: the file is caught up");

    QFile::remove(gpu_log);
    write_lines(gpu_log, 1, 3, QIODevice::WriteOnly);
    pump(900);
    check(blocks(panel) == 3 && !text(panel).contains(QStringLiteral("line 3010")),
          "a file that was replaced is read again from its start");

    QFile::remove(gpu_log);
    pump(900);
    panel.select_source(QStringLiteral("all"));
    pump(50);

    const QString guest_log = QDir{settings.logs_dir()}.filePath(QStringLiteral("guest/logs/agent.log"));
    QDir{}.mkpath(QFileInfo{guest_log}.absolutePath());
    write_lines(guest_log, 1, 2, QIODevice::WriteOnly);
    const QDir guest{QDir{settings.logs_dir()}.filePath(QStringLiteral("guest"))};
    QDir{}.mkpath(guest.filePath(QStringLiteral("av")));
    QDir{}.mkpath(guest.filePath(QStringLiteral("logs/mirrors")));
    write_lines(guest.filePath(QStringLiteral("logs/agent.1.log")), 1, 2, QIODevice::WriteOnly);
    write_lines(guest.filePath(QStringLiteral("av/avumd-100.log")), 1, 2, QIODevice::WriteOnly);
    write_lines(guest.filePath(QStringLiteral("gpu-drivers.log")), 1, 2, QIODevice::WriteOnly);
    write_lines(guest.filePath(QStringLiteral("logs/mirrors/notepad.log")), 1, 2, QIODevice::WriteOnly);
    write_lines(guest.filePath(QStringLiteral("release.json")), 1, 2, QIODevice::WriteOnly);
    panel.hide();
    panel.show();
    pump(50);
    check(panel.picker()->findText(QStringLiteral("Windows: Agent")) >= 0,
          "Windows' logs, once pulled, are in the picker by name");
    check(panel.picker()->findText(QStringLiteral("Windows: GPU drivers")) >= 0 &&
              panel.picker()->findText(QStringLiteral("Windows: Mirror notepad")) >= 0,
          "the merged GPU driver log and each mirror have one entry");
    int windows_entries = 0;
    for (int i = 0; i < panel.picker()->count(); ++i) {
        if (panel.picker()->itemText(i).startsWith(QLatin1String("Windows: "))) {
            ++windows_entries;
        }
    }
    check(windows_entries == 3, "rolled copies, per-process driver logs and non-log files stay out of the picker");
    check(panel.picker()->findText(QStringLiteral("QEMU")) >= 0 &&
              panel.picker()->findText(QStringLiteral("virtiofsd")) >= 0 &&
              panel.picker()->findText(QStringLiteral("GPU driver")) >= 0,
          "the VM's files are in the picker");

    return g_failures == 0 ? 0 : 1;
}
