#pragma once

#include <QImage>
#include <QString>

namespace lsw::manager::apps {

// How the host should mirror this app's window output back to the
// Linux desktop. ``Wgc`` (Windows Graphics Capture) is the universal
// path — works for any window, costs CPU/GPU on the guest. ``Vulkan``
// is our ICD-proxy path — zero-copy for apps that render via the
// Vulkan API (the LSW client ICD ships rendering commands over vsock
// to the host server, no per-frame readback).
enum class CaptureMethod {
    Wgc,
    Vulkan,
};

[[nodiscard]] QString capture_method_to_string(CaptureMethod m);
[[nodiscard]] CaptureMethod capture_method_from_string(const QString& s);

// One configured application. Persisted as JSON under
// ``~/.local/share/lsw/apps/<id>.json``. The store is the source of
// truth for what the manager UI shows; agent-discovered apps are
// upserted into the store on each refresh so the user can override
// metadata (capture method, name, cmdline) without losing it.
struct AppConfig {
    QString id;            // stable file-name token (sha1-prefix of path or sanitised name)
    QString name;          // user-facing label
    QString path;          // Windows path, e.g. C:\Windows\notepad.exe or .lnk
    QString cmdline;       // extra args appended at launch time
    CaptureMethod capture = CaptureMethod::Wgc;
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

}  // namespace lsw::manager::apps
