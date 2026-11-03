#include "lsw/host/manager/setting_controls.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/ui/slider_spin.hpp"
#include "lsw/host/ui/toggle_switch.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>

namespace lsw::host::manager {

SettingControls::SettingControls(config::ManagerSettings& settings,
                                 config::PendingChanges& pending, QObject* parent)
    : QObject(parent), settings_(settings), pending_(pending) {
    connect(&settings_, &config::ManagerSettings::changed, this, &SettingControls::show_current);
    connect(&pending_, &config::PendingChanges::changed, this, [this] {
        for (auto it = shows_.constBegin(); it != shows_.constEnd(); ++it) {
            for (const auto& follower : it.value()) {
                if (follower.context) {
                    follower.show();
                }
            }
        }
    });
}

void SettingControls::follow(const QString& key, QObject* context, std::function<void()> show) {
    auto& followers = shows_[key];
    followers.removeIf([](const Follower& follower) { return follower.context.isNull(); });
    followers.push_back({context, std::move(show)});
}

void SettingControls::show_current(const QString& key) {
    if (const auto it = shows_.constFind(key); it != shows_.constEnd()) {
        for (const auto& follower : it.value()) {
            if (follower.context) {
                follower.show();
            }
        }
    }
}

QVariant SettingControls::current(const QString& key) const {
    return pending_.staged_or_current(key);
}

void SettingControls::put(const QString& key, const QVariant& value) {
    if (config::ManagerSettings::needs_restart(key)) {
        pending_.stage(key, value);
        return;
    }
    settings_.set_value(key, value);
}

ui::SliderSpin* SettingControls::slider(const QString& key, const ui::MachineLimits::Range& range,
                                        const QString& suffix) {
    auto* slider = new ui::SliderSpin(range.min, range.max, range.step, suffix);
    slider->row()->setSpacing(12);
    slider->spin()->setFixedWidth(96);
    slider->spin()->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    slider->spin()->setObjectName(QStringLiteral("SliderValue"));
    slider->set_value(current(key).toInt());
    connect(slider, &ui::SliderSpin::value_changed, this,
            [this, key](int value) { put(key, value); });
    follow(key, slider, [this, key, slider] {
        const QSignalBlocker quiet(slider);
        slider->set_value(current(key).toInt());
    });
    return slider;
}

QComboBox* SettingControls::choice(const QString& key, const Choices& options) {
    auto* combo = new QComboBox;
    for (const auto& option : options) {
        combo->addItem(option.first, option.second);
    }
    const int index = combo->findData(current(key));
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->view()->setMinimumWidth(combo->sizeHint().width());
    combo->setToolTip(combo->currentText());
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int i) {
        combo->setToolTip(combo->itemText(i));
        put(key, combo->itemData(i));
    });
    follow(key, combo, [this, key, combo] {
        const QSignalBlocker quiet(combo);
        const int at = combo->findData(current(key));
        combo->setCurrentIndex(at >= 0 ? at : 0);
        combo->setToolTip(combo->currentText());
    });
    return combo;
}

ui::ToggleSwitch* SettingControls::toggle(const QString& key) {
    auto* box = new ui::ToggleSwitch;
    box->setChecked(current(key).toBool());
    connect(box, &ui::ToggleSwitch::toggled, this, [this, key](bool on) { put(key, on); });
    follow(key, box, [this, key, box] {
        const QSignalBlocker quiet(box);
        box->setChecked(current(key).toBool());
    });
    return box;
}

QLineEdit* SettingControls::text(const QString& key) {
    auto* edit = new QLineEdit(current(key).toString());
    connect(edit, &QLineEdit::editingFinished, this, [this, key, edit] { put(key, edit->text()); });
    follow(key, edit, [this, key, edit] {
        if (!edit->hasFocus()) {
            edit->setText(current(key).toString());
        }
    });
    return edit;
}

ui::SegmentedControl* SettingControls::segmented(
    const QString& key, const QVector<ui::SegmentedControl::Option>& options) {
    auto* control = new ui::SegmentedControl;
    control->set_options(options);
    control->set_current(current(key));
    connect(control, &ui::SegmentedControl::chosen, this,
            [this, key](const QVariant& value) { put(key, value); });
    follow(key, control, [this, key, control] { control->set_current(current(key)); });
    return control;
}

}
