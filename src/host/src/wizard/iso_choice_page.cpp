#include "lsw/host/wizard/iso_choice_page.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/tools/download_image_dialog.hpp"
#include "lsw/host/tools/trim_image_dialog.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::wizard {

namespace {

using ui::Theme;

// One of the two tool cards under the field: a name, a sentence, and
// the button that opens it.
QWidget* tool_card(const QString& title, const QString& blurb, QPushButton* button) {
    auto* frame = new QFrame;
    Theme::surface(frame, Theme::Role::kPanel);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(13, 13, 13, 13);
    layout->setSpacing(7);

    auto* name = new QLabel(title);
    QFont font = name->font();
    font.setWeight(QFont::Medium);
    name->setFont(font);

    auto* text = new QLabel(blurb);
    text->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    text->setWordWrap(true);

    layout->addWidget(name);
    layout->addWidget(text, 1);
    layout->addWidget(button, 0, Qt::AlignLeft);
    return frame;
}

}  // namespace

IsoChoicePage::IsoChoicePage(Wizard& wizard, QWidget* parent)
    : WizardPage(parent), wizard_(wizard) {

    auto* root =
        install_chrome(QStringLiteral("Which Windows image should be installed"),
                       QStringLiteral("Any retail or evaluation Windows 10 or 11 ISO. If you do "
                                      "not have one, download it here."));

    root->addWidget(ui::Widgets::caption(QStringLiteral("Image")));

    auto* row = new QHBoxLayout;
    path_edit_ = new QLineEdit;
    path_edit_->setPlaceholderText(QStringLiteral("Path to a Windows ISO"));
    auto* browse = new QPushButton(QStringLiteral("Browse"));
    row->addWidget(path_edit_, 1);
    row->addWidget(browse);
    root->addLayout(row);

    detail_ = ui::Widgets::meta(QString{}, true);
    root->addWidget(detail_);

    root->addWidget(ui::Widgets::separator());

    root->addWidget(ui::Widgets::caption(QStringLiteral("Image tools, optional")));

    auto* download_button = new QPushButton(QStringLiteral("Download an image"));
    trim_button_ = new QPushButton(QStringLiteral("Trim this image"));

    auto* tools = new QHBoxLayout;
    tools->setSpacing(10);
    tools->addWidget(tool_card(
        QStringLiteral("No image yet"),
        QStringLiteral("Fetch one from microsoft.com, into any folder you choose."),
        download_button));
    tools->addWidget(tool_card(
        QStringLiteral("Smaller guest"),
        QStringLiteral("Cut the bundled applications, or strip the image down to the "
                       "minimum. Writes a new ISO and leaves this one alone."),
        trim_button_));
    root->addLayout(tools);
    root->addStretch(1);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select a Windows ISO"), QString{},
            QStringLiteral("ISO files (*.iso)"));
        if (!path.isEmpty()) {
            prepared_ = false;
            path_edit_->setText(path);
        }
    });
    connect(download_button, &QPushButton::clicked, this,
            &IsoChoicePage::open_download_tool);
    connect(trim_button_, &QPushButton::clicked, this, &IsoChoicePage::open_trim_tool);
    connect(path_edit_, &QLineEdit::textChanged, this, [this] {
        describe_image();
        Q_EMIT completeChanged();
    });

    describe_image();
}

void IsoChoicePage::initializePage() {
    if (path_edit_->text().isEmpty() && !wizard_.state().chosen_iso_path.isEmpty()) {
        path_edit_->setText(wizard_.state().chosen_iso_path);
    }
}

void IsoChoicePage::describe_image() {
    const QFileInfo info{path_edit_->text()};
    trim_button_->setEnabled(info.exists() && info.isFile());
    if (!info.exists() || !info.isFile()) {
        detail_->setText(path_edit_->text().isEmpty()
                             ? QString{}
                             : QStringLiteral("There is no file at that path."));
        return;
    }
    detail_->setText(ui::Format::gib(info.size(), 2)
                     + (prepared_ ? QStringLiteral(", trimmed and ready") : QString{}));
}

void IsoChoicePage::open_download_tool() {
    tools::DownloadImageDialog dialog{wizard_.services().settings(), this};
    if (dialog.exec() == QDialog::Accepted && dialog.use_for_install()
        && !dialog.result_path().isEmpty()) {
        // A fresh download has not been through the preparation pass,
        // so the pipeline still has to run it.
        prepared_ = false;
        path_edit_->setText(dialog.result_path());
    }
}

void IsoChoicePage::open_trim_tool() {
    tools::TrimImageDialog dialog{wizard_.services().settings(), path_edit_->text(),
                                  wizard_.state().image_index, this};
    if (dialog.exec() == QDialog::Accepted && !dialog.result_path().isEmpty()) {
        if (dialog.use_for_install()) {
            // The tool rebuilt the image, which is also what writes the
            // setup bypasses, so the pipeline can skip its own pass.
            prepared_ = true;
            path_edit_->setText(dialog.result_path());
        }
    }
}

bool IsoChoicePage::isComplete() const {
    const QFileInfo info{path_edit_->text()};
    return info.exists() && info.isFile();
}

bool IsoChoicePage::validatePage() {
    auto& state = wizard_.state();
    state.chosen_iso_path = path_edit_->text();
    state.iso_prepared = prepared_;
    return true;
}

int IsoChoicePage::nextId() const {
    return Wizard::VmSize;
}

}  // namespace lsw::host::wizard
