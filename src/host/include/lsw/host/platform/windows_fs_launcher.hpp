#pragma once

#include <QString>
#include <functional>

namespace lsw::host::platform {
class WindowsFsLauncher {
public:
    struct Result {
        bool ok = false;
        QString error;
    };

    using Done = std::function<void(const Result&)>;

    WindowsFsLauncher() = default;
    virtual ~WindowsFsLauncher() = default;
    WindowsFsLauncher(const WindowsFsLauncher&) = delete;
    WindowsFsLauncher& operator=(const WindowsFsLauncher&) = delete;

    [[nodiscard]] virtual QString unavailable() const = 0;

    [[nodiscard]] virtual bool owns_mount() const = 0;

    virtual void mount(const QString& share, const QString& user, const QString& password,
                       Done done) = 0;

    virtual void unmount(Done done) = 0;

    [[nodiscard]] virtual Result unmount_now() = 0;
};

}  // namespace lsw::host::platform
