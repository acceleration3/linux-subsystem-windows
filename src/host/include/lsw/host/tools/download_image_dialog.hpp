#pragma once

#include "lsw/host/provision/iso/microsoft_client.hpp"
#include "lsw/host/provision/iso/source_fetcher.hpp"
#include "lsw/host/provision/iso/windows_edition.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace lsw::host::config { class Settings; }
namespace lsw::host::provision::iso { class Downloader; }

namespace lsw::host::tools {
class DownloadImageDialog : public QDialog {
    Q_OBJECT
public:
    explicit DownloadImageDialog(config::Settings& settings, QWidget* parent = nullptr);

    [[nodiscard]] QString result_path() const noexcept { return result_path_; }
    [[nodiscard]] bool use_for_install() const;

private:
    void start();
    void set_busy(bool busy);
    void block(const QString& reason);
    void refresh_free_space();

    config::Settings& settings_;
    provision::iso::IsoSourceFetcher fetcher_;
    provision::iso::MicrosoftClient ms_;
    QList<provision::iso::WindowsEdition> editions_;
    QList<provision::iso::MsSku> skus_;
    provision::iso::Downloader* downloader_ = nullptr;

    QString result_path_;
    bool ms_ready_ = false;
    bool resolving_ = false;
    QElapsedTimer clock_;
    qint64 resumed_from_ = -1;

    QComboBox* edition_combo_ = nullptr;
    QComboBox* language_combo_ = nullptr;
    QLabel* source_status_ = nullptr;
    QLineEdit* output_dir_ = nullptr;
    QLineEdit* output_name_ = nullptr;
    QLabel* space_ = nullptr;
    QCheckBox* use_box_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* progress_text_ = nullptr;
    QLabel* rate_ = nullptr;
    QLabel* elapsed_ = nullptr;
    QLabel* remaining_ = nullptr;
    QPushButton* run_button_ = nullptr;
    QPushButton* close_button_ = nullptr;
};

}
