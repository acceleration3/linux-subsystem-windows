#include "lsw/host/apps/app_config_store.hpp"

#include <QTemporaryDir>
#include <cstdio>

using lsw::host::apps::AppConfig;
using lsw::host::apps::AppConfigStore;
using lsw::host::apps::AppEntry;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

AppEntry entry(const QString& name) {
    AppEntry e;
    e.id = QStringLiteral("C:\\Apps\\") + name + QStringLiteral(".exe");
    e.name = name;
    e.exe_path = e.id;
    return e;
}

bool shown(const AppConfigStore& store, const QString& id) {
    return store.find(id) != nullptr;
}

bool hidden(const AppConfigStore& store, const QString& id) {
    for (const auto& cfg : store.hidden()) {
        if (cfg.id == id) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    QTemporaryDir dir;
    const QList<AppEntry> windows{entry(QStringLiteral("Notepad")), entry(QStringLiteral("Paint")),
                                  entry(QStringLiteral("Word"))};
    const QString notepad = AppConfig::make_id(windows[0].id);
    const QString paint = AppConfig::make_id(windows[1].id);
    const QString word = AppConfig::make_id(windows[2].id);

    AppConfigStore store{dir.path()};
    store.merge_from_agent(windows);
    check(store.configs().size() == 3 && store.hidden().isEmpty(),
          "the apps Windows reports are listed");

    store.hide(notepad);
    check(!shown(store, notepad) && hidden(store, notepad), "a removed app leaves the list");
    store.merge_from_agent(windows);
    check(!shown(store, notepad) && hidden(store, notepad) && store.configs().size() == 2,
          "a refresh that reports it again leaves it removed");

    AppConfigStore reopened{dir.path()};
    check(!shown(reopened, notepad) && hidden(reopened, notepad),
          "it stays removed when the manager starts again");

    store.merge_from_agent({windows[1], windows[2]});
    check(hidden(store, notepad), "it stays removed while Windows does not report it");
    store.merge_from_agent(windows);
    check(!shown(store, notepad), "and when Windows reports it again later");

    store.restore(notepad);
    check(shown(store, notepad) && !hidden(store, notepad), "a restored app is back in the list");
    store.merge_from_agent(windows);
    check(shown(store, notepad), "a refresh keeps a restored app");

    store.hide(paint);
    store.hide(word);
    store.restore_all();
    check(store.configs().size() == 3 && store.hidden().isEmpty(),
          "restoring all brings every removed app back");

    AppConfig edited = *store.find(word);
    edited.cmdline = QStringLiteral("/safe");
    store.upsert(edited);
    store.hide(word);
    store.restore(word);
    check(store.find(word) != nullptr && store.find(word)->cmdline == QStringLiteral("/safe"),
          "a restored app keeps its settings");

    store.hide(paint);
    AppConfig again;
    again.name = QStringLiteral("Paint");
    again.path = windows[1].exe_path;
    again.id = paint;
    store.upsert(again);
    check(shown(store, paint) && !hidden(store, paint),
          "adding a removed app again brings it back");

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("app config store: all checks passed\n");
    return 0;
}
