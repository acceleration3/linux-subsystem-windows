#pragma once

#include "lsw/host/config/display_modes.hpp"

#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>
#include <functional>
#include <optional>

namespace lsw::host::config {
class ManagerSettings;
}

namespace lsw::host::services {
class OneToOneDesktop : public QObject {
    Q_OBJECT
public:
    using Mode = config::DisplayMode;
    using Answer = std::function<void(const std::optional<Mode>& now)>;

    struct Guest {
        std::function<bool(const QString& modes)> offer;
        std::function<void(const std::optional<Mode>& wanted, Answer done)> resolution;
        std::function<std::optional<Mode>(const std::optional<Mode>& wanted)> resolution_now;
    };

    static constexpr int kSettleMs = 120;

    OneToOneDesktop(config::ManagerSettings& settings, Guest guest, QObject* parent = nullptr,
                    int settle_ms = kSettleMs);

    [[nodiscard]] QString modes(const std::optional<Mode>& output);

    void opened();
    void resized(const QSize& pixels);
    void closed();
    void guest_ready();
    void shutdown();

    [[nodiscard]] const std::optional<Mode>& applied() const noexcept { return target_; }

private:
    struct Switch {
        std::optional<Mode> wanted;
        Answer done;
    };

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] std::optional<Mode> saved() const;
    void follow();
    void apply(const Mode& wanted);
    void ask(const std::optional<Mode>& wanted, Answer done);
    void restore();
    void forget();

    config::ManagerSettings& settings_;
    Guest guest_;
    QTimer settle_;
    std::optional<Mode> output_;
    std::optional<Mode> room_;
    std::optional<Mode> target_;
    std::optional<Switch> waiting_;
    bool open_ = false;
    bool asking_ = false;
    bool busy_ = false;
};

}  // namespace lsw::host::services
