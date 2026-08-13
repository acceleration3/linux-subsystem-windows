#include "lsw/host/apps/app_config_store.hpp"

#include "lsw/base/log.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>

namespace lsw::host::apps {

namespace {

QString png_base64_from_image(const QImage& img) {
    if (img.isNull()) {
        return {};
    }
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

}  // namespace

AppConfigStore::AppConfigStore(QString dir, QObject* parent)
    : QObject(parent), dir_(std::move(dir)) {
    QDir().mkpath(dir_);
    reload();
}

QString AppConfigStore::file_for(const QString& id) const {
    return QDir(dir_).filePath(id + ".json");
}

void AppConfigStore::reload() {
    QList<AppConfig> next;
    QDir d{dir_};
    const auto entries = d.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& fn : entries) {
        QFile f{d.filePath(fn)};
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        bool ok = false;
        const auto cfg = AppConfig::from_json(QString::fromUtf8(f.readAll()), &ok);
        if (ok) {
            next.append(cfg);
        }
    }
    if (next.size() != configs_.size()) {
        configs_ = std::move(next);
        Q_EMIT changed();
        return;
    }
    configs_ = std::move(next);
    Q_EMIT changed();
}

AppConfig* AppConfigStore::find(const QString& id) {
    for (auto& c : configs_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const AppConfig* AppConfigStore::find(const QString& id) const {
    for (const auto& c : configs_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

bool AppConfigStore::write_file(const AppConfig& cfg) {
    QFile f{file_for(cfg.id)};
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("could not write app config {}: {}", file_for(cfg.id).toStdString(),
                 f.errorString().toStdString());
        return false;
    }
    f.write(cfg.to_json().toUtf8());
    return true;
}

void AppConfigStore::upsert(AppConfig cfg) {
    if (cfg.id.isEmpty()) {
        cfg.id = AppConfig::make_id(cfg.path.isEmpty() ? cfg.name : cfg.path);
    }
    if (!write_file(cfg)) {
        return;
    }
    if (auto* existing = find(cfg.id); existing != nullptr) {
        *existing = std::move(cfg);
    } else {
        configs_.append(std::move(cfg));
    }
    Q_EMIT changed();
}

void AppConfigStore::remove(const QString& id) {
    auto* existing = find(id);
    if (existing == nullptr) {
        return;
    }
    QFile::remove(file_for(id));
    for (auto it = configs_.begin(); it != configs_.end(); ++it) {
        if (it->id == id) {
            configs_.erase(it);
            break;
        }
    }
    Q_EMIT changed();
}

void AppConfigStore::merge_from_agent(const QList<AppEntry>& entries) {
    const auto now = QDateTime::currentSecsSinceEpoch();
    bool any_changed = false;
    for (const auto& e : entries) {
        const QString id = AppConfig::make_id(e.id.isEmpty() ? e.name : e.id);
        if (auto* existing = find(id); existing != nullptr) {
            existing->last_seen_unix = now;
            // Refresh agent-derived fields only if the user hasn't
            // diverged from the default. Names/cmdlines stay as-is.
            if (existing->discovered_via_agent && !e.icon.isNull()) {
                const QString fresh = png_base64_from_image(e.icon);
                if (!fresh.isEmpty() && fresh != existing->icon_png_base64) {
                    existing->icon_png_base64 = fresh;
                    any_changed = true;
                }
            }
            (void)write_file(*existing);
            continue;
        }
        AppConfig cfg;
        cfg.id = id;
        cfg.name = e.name;
        cfg.path = e.exe_path;
        cfg.capture = CaptureMethod::Wgc;  // default for everything
        cfg.discovered_via_agent = true;
        cfg.last_seen_unix = now;
        cfg.icon_png_base64 = png_base64_from_image(e.icon);
        if (write_file(cfg)) {
            configs_.append(std::move(cfg));
            any_changed = true;
        }
    }
    if (any_changed) {
        Q_EMIT changed();
    }
}

}  // namespace lsw::host::apps
