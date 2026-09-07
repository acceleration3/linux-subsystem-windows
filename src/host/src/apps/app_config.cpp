#include "lsw/host/apps/app_config.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRegularExpression>

#include <nlohmann/json.hpp>

namespace lsw::host::apps {

using nlohmann::json;

QImage AppConfig::icon_image() const {
    if (icon_png_base64.isEmpty()) {
        return {};
    }
    QImage img;
    img.loadFromData(QByteArray::fromBase64(icon_png_base64.toLatin1()), "PNG");
    return img;
}

QString AppConfig::make_id(const QString& path_or_name) {
    // Stable, file-system-safe id: hex-sha1 of the input. We prefix
    // with a short slug from the basename so the file names are
    // human-readable when listing the directory.
    QString slug = path_or_name.section('\\', -1).section('/', -1);
    slug.replace(QRegularExpression("\\.[^.]+$"), {});  // strip extension
    slug = slug.toLower();
    static const QRegularExpression non_token("[^a-z0-9]+");
    slug.replace(non_token, "-");
    while (slug.startsWith('-')) {
        slug.remove(0, 1);
    }
    while (slug.endsWith('-')) {
        slug.chop(1);
    }
    if (slug.isEmpty()) {
        slug = "app";
    }
    if (slug.size() > 32) {
        slug.truncate(32);
    }
    const auto hash = QCryptographicHash::hash(path_or_name.toUtf8(), QCryptographicHash::Sha1)
                          .toHex()
                          .left(8);
    return slug + "-" + QString::fromLatin1(hash);
}

QString AppConfig::to_json() const {
    json j = json::object();
    j["id"] = id.toStdString();
    j["name"] = name.toStdString();
    j["path"] = path.toStdString();
    j["cmdline"] = cmdline.toStdString();
    j["env"] = env.toStdString();
    j["icon_png_base64"] = icon_png_base64.toStdString();
    j["discovered_via_agent"] = discovered_via_agent;
    j["last_seen_unix"] = static_cast<std::int64_t>(last_seen_unix);
    return QString::fromStdString(j.dump(2));
}

AppConfig AppConfig::from_json(const QString& s, bool* ok) {
    AppConfig out;
    if (ok != nullptr) {
        *ok = false;
    }
    json j;
    try {
        const auto bytes = s.toUtf8();
        j = json::parse(bytes.constData(), bytes.constData() + bytes.size());
    } catch (const json::exception&) {
        return out;
    }
    if (!j.is_object()) {
        return out;
    }
    out.id = QString::fromStdString(j.value("id", std::string{}));
    out.name = QString::fromStdString(j.value("name", std::string{}));
    out.path = QString::fromStdString(j.value("path", std::string{}));
    out.cmdline = QString::fromStdString(j.value("cmdline", std::string{}));
    out.env = QString::fromStdString(j.value("env", std::string{}));
    out.icon_png_base64 = QString::fromStdString(j.value("icon_png_base64", std::string{}));
    out.discovered_via_agent = j.value("discovered_via_agent", false);
    out.last_seen_unix = j.value("last_seen_unix", static_cast<std::int64_t>(0));
    if (ok != nullptr) {
        *ok = !out.id.isEmpty();
    }
    return out;
}

}  // namespace lsw::host::apps
