#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QDialog>
#include <QString>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QComboBox;
class QCheckBox;

namespace lsw::host::config { class Settings; }

namespace lsw::host::tools {

// Trim a Windows image into a smaller one.
//
// This used to be a page in the first-run wizard, which was wrong twice
// over: trimming is not part of installing, and a wizard page can only
// run once, in one place, with no way back once it had started. It is a
// tool now. It opens from the wizard's ISO page and from Settings, it
// writes wherever it is pointed, and skipping it costs nothing.
//
// The source image is opened read only and never modified. What comes
// out is a new bootable ISO beside it, or anywhere else.
class TrimImageDialog : public QDialog {
    Q_OBJECT
public:
    TrimImageDialog(config::Settings& settings, QString source_iso, int image_index,
                    QWidget* parent = nullptr);

    // The image that was written, or empty when nothing was. The caller
    // decides what to do with it; the checkbox only says what the user
    // wanted.
    [[nodiscard]] QString result_path() const noexcept { return result_path_; }
    [[nodiscard]] bool use_for_install() const;

private:
    void start();
    void refresh_estimate();
    void set_busy(bool busy);

    config::Settings& settings_;
    QString source_iso_;
    int image_index_ = 1;
    QString result_path_;

    QLineEdit* source_edit_ = nullptr;
    QComboBox* edition_combo_ = nullptr;
    QButtonGroup* level_group_ = nullptr;
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

}  // namespace lsw::host::tools
