#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace lsw::host::config { class ManagerSettings; }

namespace lsw::host::manager {

// The guest monitor's resolutions, edited in place: one row per size,
// the preferred one marked, each movable and removable, and a way to add
// a preset or any size typed in. A live setting — every edit is written
// at once and the window sends it to the running device.
//
// The sizes accel-virt's driver would silently leave out are refused
// here, with the reason beside the field (config::DisplayModes).
class ResolutionEditor : public QWidget {
    Q_OBJECT
public:
    explicit ResolutionEditor(config::ManagerSettings& settings, QWidget* parent = nullptr);

    // Offer the host monitors' own modes (an empty list) instead of the
    // list. The list is kept while that is on, and comes back when it is
    // turned off.
    void use_host_modes(bool on);

private:
    void rebuild();
    void write(const QStringList& list);
    void move(int row, int by);
    void validate();
    // Put a size the list lacks into the boxes.
    void suggest(const QStringList& list);
    void fill_presets();

    config::ManagerSettings& settings_;
    QTableWidget* table_ = nullptr;
    QComboBox* presets_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QPushButton* add_ = nullptr;
    QLabel* reason_ = nullptr;
    // The list as it was before the host's modes were asked for.
    QStringList kept_;
    // Whether the size fields have been touched: a refusal of the
    // starting values, which nobody typed, is not worth a red line.
    bool edited_ = false;
};

}  // namespace lsw::host::manager
