#include "lsw/host/tools/download_image_dialog.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/provision/iso/downloader.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStorageInfo>
#include <QVBoxLayout>

namespace lsw::host::tools {

namespace {

using ui::Theme;

QLabel* meta(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    label->setWordWrap(true);
    return label;
}

QLabel* caption(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    return label;
}

// One of the three figures under the progress bar. A long wait earns
// real numbers.
QWidget* tile(const QString& name, QLabel*& value_out) {
    auto* box = new QFrame;
    box->setObjectName(QString::fromLatin1(Theme::Role::kPanel));
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(11, 9, 11, 9);
    layout->setSpacing(2);
    layout->addWidget(caption(name));
    value_out = new QLabel(QStringLiteral("—"));
    value_out->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    value_out->setStyleSheet(QStringLiteral("font-size: 16px;"));
    layout->addWidget(value_out);
    return box;
}

QString duration(qint64 seconds) {
    if (seconds < 0) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

DownloadImageDialog::DownloadImageDialog(config::Settings& settings, QWidget* parent)
    : QDialog(parent), settings_(settings) {
    setWindowTitle(QStringLiteral("Download a Windows image"));
    setModal(true);
    setMinimumWidth(620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    // ---- what to fetch --------------------------------------------
    auto* pickers = new QHBoxLayout;
    pickers->setSpacing(12);

    auto* edition_col = new QVBoxLayout;
    edition_col->setSpacing(5);
    edition_col->addWidget(caption(QStringLiteral("Edition")));
    edition_combo_ = new QComboBox;
    edition_combo_->setPlaceholderText(QStringLiteral("Select an edition"));
    edition_combo_->setEnabled(false);
    edition_col->addWidget(edition_combo_);

    auto* language_col = new QVBoxLayout;
    language_col->setSpacing(5);
    language_col->addWidget(caption(QStringLiteral("Language")));
    language_combo_ = new QComboBox;
    language_combo_->setEnabled(false);
    language_col->addWidget(language_combo_);

    pickers->addLayout(edition_col, 1);
    pickers->addLayout(language_col, 1);
    root->addLayout(pickers);

    source_status_ = meta(QStringLiteral("Asking microsoft.com for the current editions."));
    root->addWidget(source_status_);

    // ---- where it goes --------------------------------------------
    root->addWidget(caption(QStringLiteral("Save to")));
    auto* out_row = new QHBoxLayout;
    output_dir_ = new QLineEdit(settings_.isos_dir());
    auto* choose = new QPushButton(QStringLiteral("Choose folder"));
    out_row->addWidget(output_dir_, 1);
    out_row->addWidget(choose);
    root->addLayout(out_row);

    output_name_ = new QLineEdit(QStringLiteral("windows.iso"));
    root->addWidget(output_name_);
    space_ = meta(QString{});
    root->addWidget(space_);

    use_box_ = new QCheckBox(
        QStringLiteral("Use this image for the install when the dialog closes"));
    use_box_->setChecked(true);
    root->addWidget(use_box_);

    // ---- progress -------------------------------------------------
    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setVisible(false);
    root->addWidget(bar_);
    progress_text_ = meta(QString{});
    root->addWidget(progress_text_);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(8);
    tiles->addWidget(tile(QStringLiteral("Rate"), rate_), 1);
    tiles->addWidget(tile(QStringLiteral("Elapsed"), elapsed_), 1);
    tiles->addWidget(tile(QStringLiteral("Remaining"), remaining_), 1);
    root->addLayout(tiles);

    root->addStretch(1);

    auto* footer = new QHBoxLayout;
    footer->addWidget(meta(QStringLiteral("Resumes if the connection drops.")), 1);
    close_button_ = new QPushButton(QStringLiteral("Close"));
    run_button_ = new QPushButton(QStringLiteral("Download"));
    run_button_->setObjectName(QString::fromLatin1(Theme::Role::kPrimary));
    run_button_->setEnabled(false);
    footer->addWidget(close_button_);
    footer->addWidget(run_button_);
    root->addLayout(footer);

    connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);
    connect(run_button_, &QPushButton::clicked, this, &DownloadImageDialog::start);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Where to save the image"), output_dir_->text());
        if (!dir.isEmpty()) {
            output_dir_->setText(dir);
            refresh_free_space();
        }
    });

    // ---- Microsoft ------------------------------------------------
    connect(&fetcher_, &provision::iso::IsoSourceFetcher::ready, this,
            [this](const QList<provision::iso::WindowsEdition>& editions) {
                editions_ = editions;
                edition_combo_->clear();
                for (const auto& edition : editions_) {
                    edition_combo_->addItem(edition.version.isEmpty()
                                                ? edition.display
                                                : QStringLiteral("%1, %2").arg(
                                                      edition.version, edition.display));
                }
                edition_combo_->setCurrentIndex(-1);
                edition_combo_->setEnabled(true);
                source_status_->setText(
                    QStringLiteral("%1 editions listed by microsoft.com. Pick one.")
                        .arg(editions_.size()));
            });
    connect(&fetcher_, &provision::iso::IsoSourceFetcher::failed, this,
            [this](const QString& message) {
                block(QStringLiteral("microsoft.com refused our request (%1). They "
                                     "periodically block addresses from the download "
                                     "portal; supply a local ISO instead.")
                          .arg(message));
            });

    connect(&ms_, &provision::iso::MicrosoftClient::initialized, this, [this] {
        ms_ready_ = true;
        // The edition may already have been chosen while the session
        // was still opening.
        if (edition_combo_->currentIndex() >= 0) {
            Q_EMIT edition_combo_->currentIndexChanged(edition_combo_->currentIndex());
        }
    });
    connect(&ms_, &provision::iso::MicrosoftClient::initialization_failed, this,
            [this](const QString& message) {
                block(QStringLiteral("Could not open a session with microsoft.com: %1")
                          .arg(message));
            });
    connect(&ms_, &provision::iso::MicrosoftClient::skus_ready, this,
            [this](const QList<provision::iso::MsSku>& skus) {
                skus_ = skus;
                language_combo_->clear();
                int english = 0;
                for (int i = 0; i < skus_.size(); ++i) {
                    const auto& sku = skus_.at(i);
                    language_combo_->addItem(sku.product_name.isEmpty()
                                                 ? sku.language
                                                 : QStringLiteral("%1 (%2)").arg(
                                                       sku.product_name, sku.language));
                    if (english == 0
                        && sku.language.contains(QLatin1String("English"),
                                                 Qt::CaseInsensitive)) {
                        english = i;
                    }
                }
                language_combo_->setCurrentIndex(english);
                language_combo_->setEnabled(true);
                run_button_->setEnabled(true);
                source_status_->setText(QStringLiteral("Pick a language and press Download."));
            });
    connect(&ms_, &provision::iso::MicrosoftClient::download_url_ready, this,
            [this](const QString& url) {
                resolving_ = false;
                const QString dest = QDir(output_dir_->text()).filePath(output_name_->text());
                QDir().mkpath(output_dir_->text());
                clock_.start();
                downloader_ = new provision::iso::Downloader(this);
                connect(downloader_, &provision::iso::Downloader::progress, this,
                        [this](qint64 received, qint64 total) {
                            if (total > 0) {
                                bar_->setValue(static_cast<int>((received * 100) / total));
                                progress_text_->setText(
                                    QStringLiteral("%1 GiB of %2 GiB")
                                        .arg(received / (1024.0 * 1024 * 1024), 0, 'f', 2)
                                        .arg(total / (1024.0 * 1024 * 1024), 0, 'f', 2));
                            }
                            const qint64 ms = clock_.elapsed();
                            if (ms > 0) {
                                const double per_second = (received * 1000.0) / ms;
                                rate_->setText(QStringLiteral("%1 MiB/s")
                                                   .arg(per_second / (1024 * 1024), 0, 'f', 1));
                                elapsed_->setText(duration(ms / 1000));
                                if (total > received && per_second > 0) {
                                    remaining_->setText(duration(
                                        static_cast<qint64>((total - received) / per_second)));
                                }
                            }
                        });
                connect(downloader_, &provision::iso::Downloader::failed, this,
                        [this](const QString& message) {
                            progress_text_->setText(
                                QStringLiteral("Download failed: %1").arg(message));
                            set_busy(false);
                        });
                connect(downloader_, &provision::iso::Downloader::finished, this,
                        [this](const QString& path) {
                            result_path_ = path;
                            bar_->setValue(100);
                            progress_text_->setText(QStringLiteral("Saved to %1").arg(path));
                            accept();
                        });
                downloader_->start(url, dest);
            });
    connect(&ms_, &provision::iso::MicrosoftClient::failed, this,
            [this](const QString& message) {
                resolving_ = false;
                source_status_->setText(QStringLiteral("microsoft.com error: %1").arg(message));
                set_busy(false);
            });
    connect(&ms_, &provision::iso::MicrosoftClient::banned_detected, this, [this] {
        block(QStringLiteral("Temporarily blocked from the Microsoft download portal after "
                             "too many automated requests. Supply a local ISO for now."));
    });

    connect(edition_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        skus_.clear();
        language_combo_->clear();
        language_combo_->setEnabled(false);
        run_button_->setEnabled(false);
        if (index < 0 || index >= editions_.size() || !ms_ready_) {
            return;
        }
        const auto& edition = editions_.at(index);
        if (edition.product_edition_ids.isEmpty()) {
            return;
        }
        source_status_->setText(QStringLiteral("Asking for the available languages."));
        ms_.fetch_skus(edition.product_edition_ids.first());
    });

    refresh_free_space();
    fetcher_.fetch();
    ms_.initialize();
}

bool DownloadImageDialog::use_for_install() const {
    return use_box_ != nullptr && use_box_->isChecked();
}

void DownloadImageDialog::refresh_free_space() {
    const QStorageInfo target{output_dir_->text()};
    if (!target.isValid() || target.bytesAvailable() <= 0) {
        space_->setText(QString{});
        return;
    }
    space_->setText(QStringLiteral("%1 GiB free on that volume.")
                        .arg(static_cast<double>(target.bytesAvailable())
                                 / (1024.0 * 1024 * 1024),
                             0, 'f', 0));
}

void DownloadImageDialog::set_busy(bool busy) {
    edition_combo_->setEnabled(!busy && !editions_.isEmpty());
    language_combo_->setEnabled(!busy && !skus_.isEmpty());
    output_dir_->setEnabled(!busy);
    output_name_->setEnabled(!busy);
    run_button_->setEnabled(!busy && !skus_.isEmpty());
    bar_->setVisible(busy);
}

void DownloadImageDialog::block(const QString& reason) {
    blocked_ = true;
    resolving_ = false;
    edition_combo_->setEnabled(false);
    language_combo_->setEnabled(false);
    run_button_->setEnabled(false);
    source_status_->setText(reason);
    source_status_->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(Theme::Color::kBad)));
    LSW_WARN("download tool: {}", reason.toStdString());
}

void DownloadImageDialog::start() {
    const int index = language_combo_->currentIndex();
    if (index < 0 || index >= skus_.size() || resolving_) {
        return;
    }
    if (output_dir_->text().isEmpty() || output_name_->text().isEmpty()) {
        source_status_->setText(QStringLiteral("Pick a folder and a file name first."));
        return;
    }
    resolving_ = true;
    set_busy(true);
    source_status_->setText(QStringLiteral("Resolving the download link. It is valid for "
                                           "24 hours once issued."));
    ms_.fetch_download_url(skus_.at(index));
}

}  // namespace lsw::host::tools
