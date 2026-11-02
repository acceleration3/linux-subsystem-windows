#pragma once

#include "lsw/host/platform/fuse_mount.hpp"
#include "lsw/host/platform/windows_fs_launcher.hpp"

#include <QObject>
#include <QString>
#include <memory>

namespace lsw::host::platform {
class FuseWindowsFsLauncher : public QObject, public WindowsFsLauncher {
    Q_OBJECT
public:
    FuseWindowsFsLauncher(QString mount_point, QObject* parent = nullptr);
    ~FuseWindowsFsLauncher() override;

    [[nodiscard]] QString unavailable() const override;

    [[nodiscard]] bool owns_mount() const override {
        return mount_ != nullptr && mount_->running();
    }

    void mount(const QString& share, const QString& user, const QString& password,
               Done done) override;

    void unmount(Done done) override;

    [[nodiscard]] Result unmount_now() override;

    [[nodiscard]] static QString prepare_mount_point(const QString& path);

private:
    struct Mounted {
        Result result;
        std::shared_ptr<FuseMount> mount;
    };

    [[nodiscard]] static Mounted build(const QString& share, const QString& user,
                                       const QString& password, const QString& mount_point);
    [[nodiscard]] static Result release(const std::shared_ptr<FuseMount>& mount,
                                        const QString& mount_point);

    QString mount_point_;
    std::shared_ptr<FuseMount> mount_;
};

}  // namespace lsw::host::platform
