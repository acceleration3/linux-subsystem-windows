#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace lsw::host::apps {
struct AppConfig {
    QString id;
    QString name;
    QString path;
    QString aumid;
    QString cmdline;
    QString env;
    QString workdir;
    QString icon_png_base64;
    bool discovered_via_agent = false;
    qint64 last_seen_unix = 0;

    bool publish_entry = true;
    QString mime_types;
    QStringList file_extensions;

    [[nodiscard]] const QString& target() const noexcept { return aumid.isEmpty() ? path : aumid; }

    [[nodiscard]] QImage icon_image() const;

    [[nodiscard]] QString to_json() const;
    [[nodiscard]] static AppConfig from_json(const QString& s, bool* ok = nullptr);

    [[nodiscard]] static QString make_id(const QString& path_or_name);
};

}
