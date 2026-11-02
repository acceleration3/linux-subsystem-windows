#include "lsw/host/manager/collect_logs_dialog.hpp"

#include "lsw/host/services/log_collector.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using services::LogCollector;
using ui::Theme;
using ui::Widgets;

QPushButton* footer_button(const QString& text, const char* role = nullptr) {
    auto* button = new QPushButton(text);
    button->setCursor(Qt::PointingHandCursor);
    if (role != nullptr) {
        Widgets::set_role(button, role);
    }
    return button;
}

}

CollectLogsDialog::CollectLogsDialog(LogCollector& collector, QWidget* parent)
    : QDialog(parent), collector_(collector) {
    setWindowTitle(tr("Collect logs"));
    setMinimumWidth(480);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetFixedSize);
    root->setContentsMargins(22, 20, 22, 16);
    root->setSpacing(14);

    auto* heading = new QLabel(tr("Collect logs"));
    Widgets::set_role(heading, Theme::Role::kHeading);
    root->addWidget(heading);

    steps_ = new QWidget;
    auto* steps_layout = new QVBoxLayout(steps_);
    steps_layout->setContentsMargins(0, 0, 0, 0);
    steps_layout->setSpacing(7);
    root->addWidget(steps_);

    result_ = new QWidget;
    auto* result_layout = new QVBoxLayout(result_);
    result_layout->setContentsMargins(0, 0, 0, 0);
    result_layout->setSpacing(7);
    result_layout->addWidget(Widgets::prose(tr("Saved as")));
    path_ = new QLabel;
    path_->setWordWrap(true);
    path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_layout->addWidget(path_);
    result_layout->addWidget(Widgets::meta(
        tr("Window titles and file names appear in logs. Read the archive before you share it."),
        true));
    root->addWidget(result_);

    failure_ = Widgets::prose(QString{});
    failure_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Widgets::set_role(failure_, Theme::Role::kCalloutCritical);
    root->addWidget(failure_);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    copy_ = footer_button(tr("Copy path"), Theme::Role::kQuiet);
    show_ = footer_button(tr("Show in folder"));
    retry_ = footer_button(tr("Try again"));
    close_ = footer_button(tr("Close"), Theme::Role::kPrimary);
    foot->addWidget(copy_);
    foot->addWidget(show_);
    foot->addWidget(retry_);
    foot->addWidget(close_);
    root->addLayout(foot);

    marks_.fill(Mark::Pending, LogCollector::StepCount);
    notes_.resize(LogCollector::StepCount);
    if (collector_.running()) {
        marks_[0] = Mark::Running;
    }

    connect(&collector_, &LogCollector::step_started, this, [this](int step) {
        for (int i = 0; i < step; ++i) {
            if (marks_[i] == Mark::Running) {
                marks_[i] = Mark::Done;
            }
        }
        set_mark(step, Mark::Running);
    });
    connect(&collector_, &LogCollector::step_skipped, this,
            [this](int step, const QString& why) { set_mark(step, Mark::Skipped, why); });
    connect(&collector_, &LogCollector::finished, this, [this](const QString& archive) {
        for (Mark& mark : marks_) {
            if (mark == Mark::Running || mark == Mark::Pending) {
                mark = Mark::Done;
            }
        }
        archive_ = archive;
        error_.clear();
        redraw();
    });
    connect(&collector_, &LogCollector::failed, this, [this](const QString& error) {
        for (Mark& mark : marks_) {
            if (mark == Mark::Running) {
                mark = Mark::Failed;
            }
        }
        error_ = error;
        redraw();
    });
    connect(copy_, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(archive_); });
    connect(show_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo{archive_}.absolutePath()));
    });
    connect(retry_, &QPushButton::clicked, this, &CollectLogsDialog::restart);
    connect(close_, &QPushButton::clicked, this, &QDialog::accept);
    redraw();
}

void CollectLogsDialog::restart() {
    marks_.fill(Mark::Pending);
    notes_.fill(QString{});
    archive_.clear();
    error_.clear();
    collector_.start();
    redraw();
}

void CollectLogsDialog::set_mark(int step, Mark mark, const QString& note) {
    if (step < 0 || step >= marks_.size()) {
        return;
    }
    marks_[step] = mark;
    notes_[step] = note;
    redraw();
}

void CollectLogsDialog::redraw() {
    auto* layout = steps_->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int i = 0; i < LogCollector::StepCount; ++i) {
        const QString name = LogCollector::step_text(i);
        QLabel* label = nullptr;
        switch (marks_[i]) {
        case Mark::Done:
            label = Widgets::step_line(QStringLiteral("✓"), name, Theme::Tone::kOk);
            break;
        case Mark::Skipped:
            label = Widgets::step_line(QStringLiteral("✓"),
                                       tr("%1: skipped, %2").arg(name, notes_[i]),
                                       Theme::Tone::kFaint);
            break;
        case Mark::Failed:
            label = Widgets::step_line(QStringLiteral("✗"), name, Theme::Tone::kBad);
            break;
        case Mark::Running:
            label = Widgets::step_line(QStringLiteral("●"), name, nullptr);
            break;
        case Mark::Pending:
            label = Widgets::step_line(QStringLiteral("○"), name, Theme::Tone::kFaint);
            break;
        }
        layout->addWidget(label);
    }

    const bool done = !archive_.isEmpty();
    const bool failed = !error_.isEmpty();
    const bool running = collector_.running();
    result_->setVisible(done);
    path_->setText(archive_);
    failure_->setVisible(failed);
    failure_->setText(error_);
    copy_->setVisible(done);
    show_->setVisible(done);
    retry_->setVisible(failed);
    retry_->setEnabled(!running);
    close_->setVisible(true);
}

}
