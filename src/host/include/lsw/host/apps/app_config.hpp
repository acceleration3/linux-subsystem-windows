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
    // Where the program starts. Empty means the folder it lives in,
    // which is what its own shortcut would have done; a .lnk that
    // named one contributes it when the picker resolves it.
    QString workdir;
    QString icon_png_base64;     // cached icon, ASCII-clean
    bool discovered_via_agent = false;  // came from start-menu enum
    qint64 last_seen_unix = 0;          // bumped on each successful refresh

    // Whether this one gets a .desktop entry in the host's menu. The
    // global switch in Settings decides whether any of them do; this
    // decides which, because a guest with forty shortcuts in its Start
    // menu should not put forty entries in somebody's launcher.
    bool publish_entry = true;
    // Windows file types this application should open on the host,
    // one MIME type per line. Empty means it registers for none.
    QString mime_types;

    // Lossy helpers — Qt-friendly views into icon_png_base64.
    [[nodiscard]] QImage icon_image() const;

    // JSON round-trip.
    [[nodiscard]] QString to_json() const;
    [[nodiscard]] static AppConfig from_json(const QString& s, bool* ok = nullptr);

    // Stable id from a Windows path (or fall back to name).
    [[nodiscard]] static QString make_id(const QString& path_or_name);
};

}  // namespace lsw::host::apps
