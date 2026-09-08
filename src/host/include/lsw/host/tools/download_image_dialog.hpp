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

// Fetch a Windows installation image from microsoft.com.
//
// Another wizard page that should never have been one. A page can only
// run on the way to somewhere, could not be reached again once passed,
// and left a wizard that had committed to downloading with no way back.
// As a tool it runs before the wizard, or during it, or from Settings
// long afterwards, and cancelling it costs nothing because nothing
// depended on it succeeding.
//
// Everything about talking to Microsoft lives here now: the edition
// scrape, the session, the SKU list and the ban detection. The wizard's
// ISO page is one path field again.
class DownloadImageDialog : public QDialog {
    Q_OBJECT
public:
    explicit DownloadImageDialog(config::Settings& settings, QWidget* parent = nullptr);

    [[nodiscard]] QString result_path() const noexcept { return result_path_; }
    [[nodiscard]] bool use_for_install() const;
    // Microsoft rate limits the download portal. Once it has refused
    // us, saying so again is more useful than trying again.
    [[nodiscard]] bool was_blocked() const noexcept { return blocked_; }

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
    bool blocked_ = false;
    bool ms_ready_ = false;
    bool resolving_ = false;
    QElapsedTimer clock_;

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

}  // namespace lsw::host::tools
