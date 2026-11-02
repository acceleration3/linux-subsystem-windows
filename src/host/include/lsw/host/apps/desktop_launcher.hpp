#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace lsw::host::apps {

class HostMimeHandlers;

class DesktopLauncher {
public:
    [[nodiscard]] static QString entry_id(const QString& app_config_id);

    [[nodiscard]] static bool install(const AppConfig& cfg);
    [[nodiscard]] static bool install(const AppConfig& cfg, const HostMimeHandlers& handlers);

    static void set_category(const QString& name);
    [[nodiscard]] static QString category();
    static void set_publishing(bool enabled);
    [[nodiscard]] static bool publishing();
    static void set_file_associations(bool enabled);
    [[nodiscard]] static bool file_associations();

    [[nodiscard]] static QStringList mime_types_of(const AppConfig& cfg);

    [[nodiscard]] static QStringList declared_mime_types(const AppConfig& cfg,
                                                         const HostMimeHandlers& handlers);

    [[nodiscard]] static QStringList mime_types_of_extensions(const QStringList& extensions);

    struct DefaultHandler {
        QString entry;
        QString name;
    };
    [[nodiscard]] static std::optional<DefaultHandler> default_handler(const QString& mime_type);

    [[nodiscard]] static bool make_default(const AppConfig& cfg, QString* error);

    static void uninstall(const AppConfig& cfg);

    [[nodiscard]] static bool is_installed(const AppConfig& cfg);

    static void sync(const QList<AppConfig>& live);

    static void sync_manager_entry();

private:
    static void write_entries(const QList<AppConfig>& live, const HostMimeHandlers& handlers);
};

}
