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
class ResolutionEditor : public QWidget {
    Q_OBJECT
public:
    explicit ResolutionEditor(config::ManagerSettings& settings, QWidget* parent = nullptr);

    void use_host_modes(bool on);

private:
    void rebuild();
    void write(const QStringList& list);
    void move(int row, int by);
    void validate();
    void suggest(const QStringList& list);
    void fill_presets();

    config::ManagerSettings& settings_;
    QTableWidget* table_ = nullptr;
    QComboBox* presets_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QPushButton* add_ = nullptr;
    QLabel* reason_ = nullptr;
    QStringList kept_;
    bool edited_ = false;
};

}
