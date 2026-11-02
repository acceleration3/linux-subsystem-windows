#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QDialog>
#include <QVector>
#include <QString>

class QButtonGroup;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QComboBox;
class QCheckBox;

namespace lsw::host::config { class Settings; }

namespace lsw::host::tools {
class TrimImageDialog : public QDialog {
    Q_OBJECT
public:
    TrimImageDialog(config::Settings& settings, QString source_iso, QWidget* parent = nullptr);

    [[nodiscard]] QString result_path() const noexcept { return result_path_; }
    [[nodiscard]] bool use_for_install() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void start();
    void refresh_estimate();
    void list_editions();
    void set_busy(bool busy);

    config::Settings& settings_;
    QString source_iso_;
    int listing_ = 0;
    QString result_path_;

    QLineEdit* source_edit_ = nullptr;
    QComboBox* edition_combo_ = nullptr;
    QLabel* edition_note_ = nullptr;
    QButtonGroup* level_group_ = nullptr;
    QVector<QFrame*> cards_;
    QLabel* warning_ = nullptr;
    QLineEdit* output_dir_ = nullptr;
    QLineEdit* output_name_ = nullptr;
    QLabel* estimate_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPushButton* run_button_ = nullptr;
    QPushButton* close_button_ = nullptr;
    QCheckBox* use_box_ = nullptr;
};

}
