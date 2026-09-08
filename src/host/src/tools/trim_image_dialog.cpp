#include "lsw/host/tools/trim_image_dialog.hpp"

#include "lsw/host/config/settings.hpp"
#include "lsw/host/provision/debloat_stager.hpp"
#include "lsw/host/provision/install/wim_edition.hpp"
#include "lsw/host/provision/install/wim_edition_lister.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStorageInfo>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace lsw::host::tools {

namespace {

using provision::debloat::DebloatLevel;
using provision::debloat::DebloatPlan;
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

QFrame* separator() {
    auto* line = new QFrame;
    line->setObjectName(QString::fromLatin1(Theme::Role::kSeparator));
    line->setFixedHeight(1);
    return line;
}

// What the level is expected to leave behind, as a fraction of the
// source. Rough on purpose: the exact number is only known once the WIM
// has been rewritten, and a figure that moves as you choose is more
// useful than none at all.
double retained_fraction(DebloatLevel level) {
    switch (level) {
    case DebloatLevel::Maximum:
        return 0.44;
    case DebloatLevel::Sane:
        return 0.93;
    case DebloatLevel::None:
        break;
    }
    return 1.0;
}

}  // namespace

TrimImageDialog::TrimImageDialog(config::Settings& settings, QString source_iso,
                                 int image_index, QWidget* parent)
    : QDialog(parent),
      settings_(settings),
      source_iso_(std::move(source_iso)),
      image_index_(image_index) {
    setWindowTitle(QStringLiteral("Trim a Windows image"));
    setModal(true);
    setMinimumWidth(620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    // ---- source ---------------------------------------------------
    auto* source_box = new QVBoxLayout;
    source_box->setSpacing(6);
    source_box->addWidget(caption(QStringLiteral("Source image")));
    auto* source_row = new QHBoxLayout;
    source_edit_ = new QLineEdit(source_iso_);
    source_edit_->setPlaceholderText(QStringLiteral("Path to a Windows ISO"));
    auto* browse = new QPushButton(QStringLiteral("Browse"));
    source_row->addWidget(source_edit_, 1);
    source_row->addWidget(browse);
    source_box->addLayout(source_row);

    edition_combo_ = new QComboBox;
    edition_combo_->setEnabled(false);
    source_box->addWidget(edition_combo_);
    root->addLayout(source_box);
    root->addWidget(separator());

    // ---- level ----------------------------------------------------
    root->addWidget(caption(QStringLiteral("How much to remove")));
    level_group_ = new QButtonGroup(this);
    level_group_->setExclusive(true);

    const struct {
        DebloatLevel level;
        const char* title;
    } kLevels[] = {
        {DebloatLevel::None, "Keep everything"},
        {DebloatLevel::Sane, "Remove the bundled apps and the telemetry"},
        {DebloatLevel::Maximum, "Strip it to the minimum"},
    };
    for (const auto& entry : kLevels) {
        auto* radio = new QRadioButton(QString::fromLatin1(entry.title));
        level_group_->addButton(radio, static_cast<int>(entry.level));
        root->addWidget(radio);
        auto* description = meta(DebloatPlan{entry.level}.summary());
        description->setContentsMargins(24, 0, 0, 4);
        root->addWidget(description);
        if (entry.level == DebloatLevel::Sane) {
            radio->setChecked(true);
        }
    }

    warning_ = meta(QString{});
    warning_->setStyleSheet(QStringLiteral("color: #f0c393;"));
    warning_->setVisible(false);
    root->addWidget(warning_);
    root->addWidget(separator());

    // ---- output ---------------------------------------------------
    root->addWidget(caption(QStringLiteral("Write the new image to")));
    auto* out_row = new QHBoxLayout;
    output_dir_ = new QLineEdit(settings_.isos_dir());
    auto* choose = new QPushButton(QStringLiteral("Choose folder"));
    out_row->addWidget(output_dir_, 1);
    out_row->addWidget(choose);
    root->addLayout(out_row);

    output_name_ = new QLineEdit(QStringLiteral("windows-trimmed.iso"));
    root->addWidget(output_name_);
    estimate_ = meta(QString{});
    root->addWidget(estimate_);

    use_box_ = new QCheckBox(
        QStringLiteral("Use this image for the install when the dialog closes"));
    use_box_->setChecked(true);
    root->addWidget(use_box_);

    // ---- progress -------------------------------------------------
    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setVisible(false);
    root->addWidget(bar_);
    status_ = meta(QString{});
    root->addWidget(status_);

    root->addStretch(1);

    // ---- footer ---------------------------------------------------
    auto* footer = new QHBoxLayout;
    footer->addWidget(meta(QStringLiteral("The source image is read only and is never "
                                          "modified.")),
                      1);
    close_button_ = new QPushButton(QStringLiteral("Close"));
    run_button_ = new QPushButton(QStringLiteral("Trim image"));
    run_button_->setObjectName(QString::fromLatin1(Theme::Role::kPrimary));
    footer->addWidget(close_button_);
    footer->addWidget(run_button_);
    root->addLayout(footer);

    connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);
    connect(run_button_, &QPushButton::clicked, this, &TrimImageDialog::start);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select a Windows ISO"), QString{},
            QStringLiteral("ISO files (*.iso)"));
        if (!path.isEmpty()) {
            source_edit_->setText(path);
        }
    });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Where to write the new image"), output_dir_->text());
        if (!dir.isEmpty()) {
            output_dir_->setText(dir);
            refresh_estimate();
        }
    });
    connect(level_group_, &QButtonGroup::idToggled, this,
            [this](int, bool) { refresh_estimate(); });
    connect(source_edit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        source_iso_ = path;
        refresh_estimate();
    });

    // The edition list comes out of the image's own WIM, which takes a
    // few seconds; the dialog is usable while it arrives.
    auto* watcher = new QFutureWatcher<QVector<provision::install::WimEdition>>(this);
    connect(watcher, &QFutureWatcher<QVector<provision::install::WimEdition>>::finished, this,
            [this, watcher] {
                const auto editions = watcher->result();
                edition_combo_->clear();
                for (const auto& edition : editions) {
                    edition_combo_->addItem(edition.name, edition.index);
                }
                if (!editions.isEmpty()) {
                    const int wanted = edition_combo_->findData(image_index_);
                    edition_combo_->setCurrentIndex(wanted >= 0 ? wanted : 0);
                    edition_combo_->setEnabled(true);
                }
                watcher->deleteLater();
            });
    if (!source_iso_.isEmpty()) {
        const QString iso = source_iso_;
        const QString scratch = settings_.cache_dir();
        watcher->setFuture(QtConcurrent::run([iso, scratch] {
            return provision::install::WimEditionLister::list(iso, scratch, nullptr);
        }));
    }

    refresh_estimate();
}

bool TrimImageDialog::use_for_install() const {
    return use_box_ != nullptr && use_box_->isChecked();
}

void TrimImageDialog::refresh_estimate() {
    const auto level = static_cast<DebloatLevel>(level_group_->checkedId());
    warning_->setText(DebloatPlan{level}.warning());
    warning_->setVisible(!warning_->text().isEmpty());

    const qint64 source_bytes = QFileInfo(source_iso_).size();
    const QStorageInfo target{output_dir_->text()};
    QStringList parts;
    if (source_bytes > 0) {
        const double out_gib =
            (static_cast<double>(source_bytes) * retained_fraction(level))
            / (1024.0 * 1024.0 * 1024.0);
        parts << QStringLiteral("Roughly %1 GiB out").arg(out_gib, 0, 'f', 1);
    }
    if (target.isValid() && target.bytesAvailable() > 0) {
        parts << QStringLiteral("%1 GiB free on that volume")
                     .arg(static_cast<double>(target.bytesAvailable())
                              / (1024.0 * 1024.0 * 1024.0),
                          0, 'f', 0);
    }
    estimate_->setText(parts.join(QStringLiteral(". ")));
}

void TrimImageDialog::set_busy(bool busy) {
    source_edit_->setEnabled(!busy);
    edition_combo_->setEnabled(!busy && edition_combo_->count() > 0);
    output_dir_->setEnabled(!busy);
    output_name_->setEnabled(!busy);
    run_button_->setEnabled(!busy);
    for (auto* button : level_group_->buttons()) {
        button->setEnabled(!busy);
    }
    bar_->setVisible(busy);
}

void TrimImageDialog::start() {
    if (source_iso_.isEmpty() || !QFileInfo::exists(source_iso_)) {
        status_->setText(QStringLiteral("Pick a source image first."));
        return;
    }
    const QString dir = output_dir_->text();
    if (dir.isEmpty()) {
        status_->setText(QStringLiteral("Pick a folder to write into."));
        return;
    }
    QDir().mkpath(dir);

    const auto level = static_cast<DebloatLevel>(level_group_->checkedId());
    const QVariant index = edition_combo_->currentData();
    if (index.isValid()) {
        image_index_ = index.toInt();
    }

    set_busy(true);
    status_->setText(QStringLiteral("Preparing"));
    bar_->setValue(0);

    // Deletes itself once it has spoken, so a retry after a failure
    // does not leave the previous attempt parented here for the life of
    // the dialog.
    auto* stager = new provision::DebloatStager(this);
    connect(stager, &provision::DebloatStager::finished, stager, &QObject::deleteLater);
    connect(stager, &provision::DebloatStager::failed, stager, &QObject::deleteLater);
    connect(stager, &provision::DebloatStager::progress, this,
            [this](const QString& detail, int percent) {
                bar_->setValue(percent);
                status_->setText(detail);
            });
    connect(stager, &provision::DebloatStager::failed, this, [this](const QString& message) {
        // Recoverable: the source is untouched, so another level or
        // another folder can be tried immediately.
        status_->setText(QStringLiteral("Could not prepare the image: %1").arg(message));
        set_busy(false);
    });
    connect(stager, &provision::DebloatStager::finished, this, [this](const QString& path) {
        result_path_ = path;
        bar_->setValue(100);
        status_->setText(QStringLiteral("Ready: %1").arg(path));
        set_busy(false);
        close_button_->setText(QStringLiteral("Done"));
        accept();
    });
    stager->trim(source_iso_, dir, image_index_, level, output_name_->text());
}

}  // namespace lsw::host::tools
