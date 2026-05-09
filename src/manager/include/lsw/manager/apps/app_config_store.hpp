#pragma once

#include "lsw/manager/apps/app_config.hpp"
#include "lsw/manager/apps/catalog.hpp"

#include <QList>
#include <QObject>
#include <QString>

namespace lsw::manager::apps {

// Owns the app-config directory under ``~/.local/share/lsw/apps/`` —
// one ``<id>.json`` per app. The store is the source of truth for
// what the manager UI shows; agent-discovered apps are upserted on
// each refresh, so the user can override fields (capture method,
// name, cmdline) without losing them when the agent re-enumerates.
//
// Thread-affine to whichever thread constructs it (Qt + filesystem
// I/O is fine on the GUI thread for small numbers of files; if the
// directory grows pathologically we can move to a worker).
class AppConfigStore : public QObject {
    Q_OBJECT
public:
    explicit AppConfigStore(QString dir, QObject* parent = nullptr);

    // Re-reads every ``*.json`` from the directory into memory.
    // Called on construction; call again to re-sync if files are
    // edited externally. Emits ``changed`` if anything actually
    // differs.
    void reload();

    [[nodiscard]] const QList<AppConfig>& configs() const noexcept { return configs_; }

    [[nodiscard]] AppConfig* find(const QString& id);
    [[nodiscard]] const AppConfig* find(const QString& id) const;

    // Persist + take ownership. If ``id`` is empty, ``make_id`` is
    // applied first. Emits ``changed``.
    void upsert(AppConfig cfg);

    // Remove from disk + memory. Emits ``changed`` if matched.
    void remove(const QString& id);

    // Merge agent-discovered entries: for each ``e`` in ``entries``,
    // either create a new config or refresh the matched one's
    // ``last_seen_unix`` (and ``icon_png_base64`` if not user-set).
    // Configs the user created by hand (``discovered_via_agent ==
    // false``) are left alone. Emits ``changed`` if anything was
    // added or modified.
    void merge_from_agent(const QList<AppEntry>& entries);

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] QString file_for(const QString& id) const;
    bool write_file(const AppConfig& cfg);

    QString dir_;
    QList<AppConfig> configs_;
};

}  // namespace lsw::manager::apps
