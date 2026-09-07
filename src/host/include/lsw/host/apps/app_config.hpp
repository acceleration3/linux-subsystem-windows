#pragma once

#include <QImage>
#include <QString>

namespace lsw::host::apps {

// One configured application. Persisted as JSON under
// ``~/.local/share/lsw/apps/<id>.json``. The store is the source of
// truth for what the manager UI shows; agent-discovered apps are
// upserted into the store on each refresh so the user can override
// metadata (name, cmdline, environment) without losing it.
struct AppConfig {
    QString id;            // stable file-name token (sha1-prefix of path or sanitised name)
    QString name;          // user-facing label
    QString path;          // Windows path, e.g. C:\Windows\notepad.exe or .lnk
    QString cmdline;       // extra args appended at launch time
    // Per-launch environment overrides, one ``NAME=value`` per line.
    //
    // For the applications configured only through their environment —
    // Mesa's ``GALLIUM_DRIVER`` chooses the OpenGL backend, DXVK and
    // friends read their own. The alternatives both overreach: a
    // machine-wide variable applies to every process in the guest and
    // only takes effect after a logon, and a batch wrapper hides the
    // real executable from the launch tracking that scopes mirrors to
    // an instance.
    QString env;
    QString icon_png_base64;     // cached icon, ASCII-clean
    bool discovered_via_agent = false;  // came from start-menu enum
    qint64 last_seen_unix = 0;          // bumped on each successful refresh

    // Lossy helpers — Qt-friendly views into icon_png_base64.
    [[nodiscard]] QImage icon_image() const;

    // JSON round-trip.
    [[nodiscard]] QString to_json() const;
    [[nodiscard]] static AppConfig from_json(const QString& s, bool* ok = nullptr);

    // Stable id from a Windows path (or fall back to name).
    [[nodiscard]] static QString make_id(const QString& path_or_name);
};

}  // namespace lsw::host::apps
