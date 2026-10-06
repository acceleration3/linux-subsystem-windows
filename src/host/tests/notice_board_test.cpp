// Notices: one strip per key, newest first, replaced by the same key,
// gone on Dismiss or when withdrawn. ``--snapshot <dir>`` saves the board
// and the launch splash's failed state as drawn.
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/manager/notice_board.hpp"
#include "lsw/host/manager/warning_strip.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) ++failures;
}
QList<lsw::host::manager::WarningStrip*> strips(lsw::host::manager::NoticeBoard& board) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // In the board's own order, top to bottom.
    QList<lsw::host::manager::WarningStrip*> shown;
    for (int i = 0; i < board.layout()->count(); ++i) {
        auto* strip = qobject_cast<lsw::host::manager::WarningStrip*>(board.layout()->itemAt(i)->widget());
        if (strip != nullptr && !strip->isHidden()) shown << strip;
    }
    return shown;
}
QString text_of(lsw::host::manager::WarningStrip* strip) {
    QString all;
    for (auto* label : strip->findChildren<QLabel*>()) all += label->text();
    return all;
}
QPushButton* button(lsw::host::manager::WarningStrip* strip, const QString& text) {
    for (auto* b : strip->findChildren<QPushButton*>()) {
        if (b->text() == text) return b;
    }
    return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    lsw::host::ui::Theme::apply();
    const int at = app.arguments().indexOf(QStringLiteral("--snapshot"));
    const QString snapshots = at > 0 ? app.arguments().value(at + 1) : QString{};

    QWidget page;
    page.resize(760, 260);
    auto* column = new QVBoxLayout(&page);
    column->setContentsMargins(0, 0, 0, 0);
    auto* board = new lsw::host::manager::NoticeBoard;
    column->addWidget(board);
    column->addStretch(1);
    page.show();
    check(board->isHidden(), "an empty board takes no room");

    int opened_desktop = 0;
    board->post(QStringLiteral("launch:Affinity"), false, QStringLiteral("Could not open Affinity."),
                QStringLiteral("The application or its mirror exited before a window appeared."),
                {{QStringLiteral("Open desktop"), [&] { ++opened_desktop; }}});
    board->post(QStringLiteral("start"), true, QStringLiteral("libvirt would not start the guest."),
                QStringLiteral("The manager's own log says why."), {{QStringLiteral("Open the log"), [] {}}});
    auto shown = strips(*board);
    check(board->isVisible() && shown.size() == 2, "two failures, two strips");
    check(text_of(shown.front()).contains(QStringLiteral("libvirt")), "the newest is on top");
    check(button(shown.front(), QStringLiteral("Dismiss")) != nullptr &&
              button(shown.back(), QStringLiteral("Dismiss")) != nullptr,
          "every notice has a Dismiss");
    button(shown.back(), QStringLiteral("Open desktop"))->click();
    check(opened_desktop == 1, "a notice's own button does its thing");
    if (!snapshots.isEmpty()) page.grab().save(snapshots + QStringLiteral("/notices.png"));

    board->post(QStringLiteral("start"), true, QStringLiteral("libvirt would not start the guest again."),
                QStringLiteral("The manager's own log says why."));
    shown = strips(*board);
    check(shown.size() == 2 && text_of(shown.front()).contains(QStringLiteral("again")),
          "the same failure again replaces its strip");
    button(shown.front(), QStringLiteral("Dismiss"))->click();
    shown = strips(*board);
    check(shown.size() == 1 && text_of(shown.front()).contains(QStringLiteral("Affinity")),
          "Dismiss takes that one away");
    board->withdraw(QStringLiteral("launch:Affinity"));
    strips(*board);
    check(board->isHidden(), "a failure that stops being true takes its strip, and the board goes");

    if (!snapshots.isEmpty()) {
        auto* splash = new lsw::host::manager::LaunchSplash(QStringLiteral("Affinity"), QIcon{}, 37000);
        splash->reach(lsw::host::manager::LaunchSplash::Phase::GuestAnswering);
        splash->fail(QStringLiteral("The application or its mirror exited before a window appeared."));
        splash->adjustSize();
        splash->grab().save(snapshots + QStringLiteral("/splash-failed.png"));
        splash->resume();
        splash->stall(QStringLiteral("Usually a guest still finishing an update. The log will say if "
                                     "the agent crashed."));
        splash->adjustSize();
        splash->grab().save(snapshots + QStringLiteral("/splash-stalled.png"));
        splash->close();
    }
    return failures == 0 ? 0 : 1;
}
