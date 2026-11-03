#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <string>

namespace lsw::host::config { class ManagerSettings; class Settings; }
namespace lsw::host::vm { struct DomainDescription; }

namespace lsw::host::services {
class GuestBundle : public QObject {
    Q_OBJECT
public:
    struct File {
        std::string guest_path;
        QByteArray bytes;
        std::string sha256;
    };

    struct Contents {
        QString error;
        QVector<File> files;
        std::string manifest;
        std::string release;
        std::string config;
        std::string shares;
        std::string version;
    };

    struct Mount {
        QString tag;
        QString drive;
        QString name;
    };

    [[nodiscard]] static QVector<Mount> mounts_of(const vm::DomainDescription& definition);

    [[nodiscard]] static QVector<Mount> mounts_of(const config::ManagerSettings& settings);

    [[nodiscard]] static std::string staged_path(const std::string& guest_path);

    [[nodiscard]] static QString shares_argument(const QVector<Mount>& mounts);

    static constexpr const char* kSharesGuestPath = "C:\\ProgramData\\LSW\\update\\shares.txt";

    [[nodiscard]] static std::shared_ptr<const Contents> collect(const QString& adapter_names,
                                                                 const std::string& shares);

    explicit GuestBundle(config::Settings& settings, QObject* parent = nullptr);

    void build(const QVector<Mount>& mounts);

    [[nodiscard]] bool building() const noexcept { return building_; }

Q_SIGNALS:
    void built(std::shared_ptr<const GuestBundle::Contents> contents);

private:
    config::Settings& settings_;
    bool building_ = false;
};

}
