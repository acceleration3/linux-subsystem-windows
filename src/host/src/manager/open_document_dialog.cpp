#include "lsw/host/manager/open_document_dialog.hpp"

#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/info_tip.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;

constexpr int kListed = 6;

QString listed(const QStringList& lines) {
    QStringList shown = lines.mid(0, kListed);
    if (lines.size() > kListed) {
        shown << QStringLiteral("and %1 more").arg(lines.size() - kListed);
    }
    return shown.join(QLatin1Char('\n'));
}

}

OpenDocumentDialog::OpenDocumentDialog(const QStringList& files, const QStringList& folders,
                                       const QString& application, bool guest_running,
                                       int unshareable, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(files.size() == 1 ? QStringLiteral("Windows can't see this file")
                                     : QStringLiteral("Windows can't see these files"));
    setModal(true);
    setMinimumWidth(560);

    const auto chrome = ui::DialogChrome::install(this);
    auto* root = chrome.body;

    auto* names = new QVBoxLayout;
    names->setSpacing(4);
    QStringList file_names;
    for (const QString& file : files) {
        file_names << QFileInfo(file).fileName();
    }
    auto* what = new QLabel(listed(file_names));
    what->setObjectName(QString::fromLatin1(Theme::Role::kHeading));
    what->setTextInteractionFlags(Qt::TextSelectableByMouse);
    names->addWidget(what);
    auto* where = Widgets::meta(listed(folders));
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    names->addWidget(where);
    root->addLayout(names);

    const QString opener = application.isEmpty() ? QStringLiteral("the application") : application;
    const QString about =
        QStringLiteral(
            "Share %1 and %2 opens %3 itself, and saving writes %4 back here. A read-only "
            "copy stays in Windows, with its changes.%5")
            .arg(folders.size() == 1 ? QStringLiteral("it") : QStringLiteral("them"), opener,
                 files.size() == 1 ? QStringLiteral("the file") : QStringLiteral("the files"),
                 files.size() == 1 ? QStringLiteral("it") : QStringLiteral("them"),
                 guest_running ? QStringLiteral(" Sharing restarts Windows, which closes the "
                                                "applications running in it.")
                               : QString{});
    auto* headline = new QHBoxLayout;
    headline->setSpacing(8);
    headline->addWidget(new QLabel(
        folders.size() == 1 ? QStringLiteral("That folder is not shared with Windows.")
                            : QStringLiteral("Those folders are not shared with Windows.")));
    headline->addWidget(new ui::InfoTip(about));
    headline->addStretch(1);
    root->addLayout(headline);
    if (unshareable > 0) {
        auto* warning = Widgets::meta(
            QStringLiteral(
                "Windows cannot use the name of %1 of them, so %2 open as copies either way.")
                .arg(unshareable)
                .arg(unshareable == 1 ? QStringLiteral("that one") : QStringLiteral("those")),
            true);
        warning->setProperty(Theme::Tone::kProperty, Theme::Tone::kWarm);
        root->addWidget(warning);
    }

    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    auto* copy = new QPushButton(QStringLiteral("Open a read-only copy"));
    auto* share = Widgets::button(guest_running ? QStringLiteral("Share the folder and restart Windows")
                                                : QStringLiteral("Share the folder and start Windows"),
                                  Theme::Role::kPrimary);
    chrome.foot->addStretch(1);
    chrome.foot->addWidget(cancel);
    chrome.foot->addWidget(copy);
    chrome.foot->addWidget(share);
    share->setDefault(true);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(copy, &QPushButton::clicked, this, [this] {
        choice_ = Choice::Copy;
        accept();
    });
    connect(share, &QPushButton::clicked, this, [this] {
        choice_ = Choice::Share;
        accept();
    });
}

}
