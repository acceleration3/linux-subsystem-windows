#pragma once

#include "lsw/host/ui/machine_limits.hpp"
#include "lsw/host/ui/segmented_control.hpp"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QVariant>
#include <QVector>

#include <functional>

class QComboBox;
class QLineEdit;

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}

namespace lsw::host::ui {
class SliderSpin;
class ToggleSwitch;
}

namespace lsw::host::manager {
class SettingControls : public QObject {
    Q_OBJECT
public:
    using Choices = QVector<QPair<QString, QVariant>>;

    SettingControls(config::ManagerSettings& settings, config::PendingChanges& pending,
                    QObject* parent = nullptr);

    [[nodiscard]] ui::ToggleSwitch* toggle(const QString& key);
    [[nodiscard]] QComboBox* choice(const QString& key, const Choices& options);
    [[nodiscard]] QLineEdit* text(const QString& key);
    [[nodiscard]] ui::SliderSpin* slider(const QString& key, const ui::MachineLimits::Range& range,
                                         const QString& suffix);
    [[nodiscard]] ui::SegmentedControl* segmented(
        const QString& key, const QVector<ui::SegmentedControl::Option>& options);

    void follow(const QString& key, QObject* context, std::function<void()> show);
    void put(const QString& key, const QVariant& value);

    [[nodiscard]] QVariant current(const QString& key) const;

private:
    void show_current(const QString& key);

    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    struct Follower {
        QPointer<QObject> context;
        std::function<void()> show;
    };

    QHash<QString, QVector<Follower>> shows_;
};

}
