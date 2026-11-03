#include "lsw/host/manager/app_error_dialog.hpp"

#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using services::AppErrorReport;
using ui::Theme;
using ui::Widgets;

QLabel* item(const QString& text) {
    auto* label = Widgets::prose(QStringLiteral("•  ") + text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QString bits(int n) { return QStringLiteral("%1-bit").arg(n); }

void add_startup(QVBoxLayout* body, const AppErrorReport& report) {
    QStringList missing;
    QStringList others;
    for (const auto& problem : report.problems) {
        using Kind = AppErrorReport::Problem::Kind;
        const bool own = problem.importer.compare(report.image_name(), Qt::CaseInsensitive) == 0;
        switch (problem.kind) {
        case Kind::Missing: {
            const QString entry = own ? problem.module
                                      : QStringLiteral("%1 (needed by %2)").arg(problem.module, problem.importer);
            if (!missing.contains(entry)) {
                missing << entry;
            }
            break;
        }
        case Kind::MissingExport:
            others << QStringLiteral("%1 is missing from %2").arg(problem.function, problem.module);
            break;
        case Kind::WrongBitness:
            others << QStringLiteral("%1 is %2 but %3 is %4")
                          .arg(problem.module, bits(problem.module_bits), problem.importer,
                               bits(problem.image_bits));
            break;
        case Kind::Invalid:
            others << QStringLiteral("%1 is not a valid executable image").arg(problem.module);
            break;
        }
    }
    auto* list = new QVBoxLayout;
    list->setSpacing(3);
    if (!missing.isEmpty()) {
        body->addWidget(Widgets::prose(QStringLiteral("These files are missing:")));
        for (const QString& entry : std::as_const(missing)) {
            list->addWidget(item(entry));
        }
    }
    for (const QString& line : std::as_const(others)) {
        list->addWidget(item(line));
    }
    for (const QString& line : report.event_text) {
        list->addWidget(item(line));
    }
    body->addLayout(list);
    if (!report.note.isEmpty() && report.problems.isEmpty() && report.event_text.isEmpty()) {
        body->addWidget(Widgets::prose(report.note.left(1).toUpper() + report.note.mid(1) + QLatin1Char('.')));
    }
}

void add_crash(QVBoxLayout* body, const AppErrorReport& report) {
    const QString article = !report.code_name.isEmpty() &&
                                    QStringLiteral("aeiou").contains(report.code_name.at(0).toLower())
                                ? QStringLiteral("an ")
                                : QStringLiteral("a ");
    QString sentence = report.code_name.isEmpty()
                           ? QStringLiteral("It crashed")
                           : QStringLiteral("It crashed with ") + article + report.code_name;
    if (!report.module.isEmpty()) {
        sentence += QStringLiteral(" in ") + report.module;
        if (!report.offset.isEmpty()) {
            sentence += QStringLiteral(" at offset ") + report.offset;
        }
    }
    body->addWidget(Widgets::prose(sentence + QLatin1Char('.')));
    if (!report.module.isEmpty() && !report.module_version.isEmpty() &&
        report.module.compare(report.image_name(), Qt::CaseInsensitive) != 0) {
        body->addWidget(Widgets::prose(QStringLiteral("%1 is version %2.")
                                           .arg(report.module, report.module_version)));
    }
}

}

QString AppErrorDialog::heading(const AppErrorReport& report) {
    return report.kind == AppErrorReport::Kind::Startup
               ? QStringLiteral("%1 could not start").arg(report.image_name())
               : QStringLiteral("%1 stopped unexpectedly").arg(report.image_name());
}

AppErrorDialog::AppErrorDialog(const AppErrorReport& report, QWidget* parent) : QDialog(parent) {
    setWindowTitle(heading(report));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(480);

    const auto chrome = ui::DialogChrome::install(this);
    auto* root = chrome.body;
    root->setSpacing(8);

    auto* title = new QLabel(heading(report));
    title->setObjectName(QString::fromLatin1(Theme::Role::kHeading));
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(title);
    root->addSpacing(4);

    if (report.kind == AppErrorReport::Kind::Startup) {
        add_startup(root, report);
    } else {
        add_crash(root, report);
    }

    root->addSpacing(6);
    auto* meta = Widgets::meta(QStringLiteral("Exit code %1 (%2), process %3")
                                   .arg(report.exit_code_hex, report.code_name)
                                   .arg(report.pid), true);
    meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(meta);

    auto* close = Widgets::button(QStringLiteral("Close"), Theme::Role::kPrimary);
    chrome.foot->addStretch(1);
    chrome.foot->addWidget(close);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

}
