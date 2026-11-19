#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/manager/removed_apps_bar.hpp"
#include "lsw/host/manager/removed_apps_popup.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::apps::AppConfigStore;
using lsw::host::apps::AppEntry;
using lsw::host::manager::RemovedAppsBar;
using lsw::host::manager::RemovedAppsPopup;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void settle() {
    for (int i = 0; i < 20; ++i) {
        QApplication::processEvents();
    }
}

QPushButton* button(QWidget* in, const QString& starts) {
    for (auto* found : in->findChildren<QPushButton*>()) {
        if (found->isVisible() && found->text().startsWith(starts)) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    lsw::host::ui::Theme::apply();
    QTemporaryDir dir;
    AppConfigStore store{dir.path()};
    QList<AppEntry> entries;
    for (int i = 0; i < 80; ++i) {
        AppEntry entry;
        entry.name = QStringLiteral("App %1").arg(i, 2, 10, QLatin1Char('0'));
        entry.id = QStringLiteral("C:\\app%1.exe").arg(i);
        entry.exe_path = entry.id;
        entries << entry;
    }
    store.merge_from_agent(entries);
    for (const auto& cfg : QList(store.configs())) {
        store.hide(cfg.id);
    }

    RemovedAppsBar bar{store};
    bar.resize(620, 44);
    bar.show();
    settle();
    auto* popup = bar.findChild<RemovedAppsPopup*>();
    auto* open = button(&bar, QStringLiteral("Restore"));
    check(popup != nullptr && open != nullptr, "the bar offers Restore for removed apps");
    open->click();
    settle();
    auto* list = popup->findChild<QListWidget*>();
    auto* find = popup->findChild<QLineEdit*>();
    check(popup->isVisible() && list->count() == 80, "the popup lists every removed app");
    const int row = list->sizeHintForRow(0);
    check(list->height() <= RemovedAppsPopup::kVisibleRows * row + 2 * list->frameWidth(),
          "80 removed apps scroll in a list of at most 10 rows instead of filling the screen");
    check(find->isVisible(), "with many removed apps the popup can find one");

    find->setText(QStringLiteral("App 4"));
    settle();
    check(list->count() == 10, "finding narrows the list to the matching apps");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(find, &enter);
    settle();
    check(store.hidden().size() == 79 && popup->isVisible() && list->count() == 9,
          "Enter restores the first match and the popup stays open for more");
    auto* shown = button(popup, QStringLiteral("Restore the 9 shown"));
    check(shown != nullptr, "while finding, the restore button covers what is shown");
    shown->click();
    settle();
    check(store.hidden().size() == 70 && !popup->isVisible(),
          "restoring the shown apps restores exactly those");

    QStringList all_but_two;
    for (int i = 2; i < store.hidden().size(); ++i) {
        all_but_two << store.hidden().at(i).id;
    }
    store.restore(all_but_two);
    settle();
    button(&bar, QStringLiteral("Restore"))->click();
    settle();
    check(!find->isVisible() && list->count() == 2, "a short list has no find field");
    list->itemClicked(list->item(0));
    list->itemClicked(list->item(0));
    settle();
    check(store.hidden().isEmpty() && !popup->isVisible() && !bar.isVisible(),
          "restoring the last removed app closes the popup and the bar");

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("removed apps popup: all checks passed\n");
    return 0;
}
