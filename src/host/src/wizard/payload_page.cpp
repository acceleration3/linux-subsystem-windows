#include "lsw/host/wizard/payload_page.hpp"
#include "lsw/host/wizard/page_chrome.hpp"

#include "lsw/host/wizard/wizard.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QBrush>
#include "lsw/host/ui/theme.hpp"

#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

namespace {
constexpr int kColTask = 0;
constexpr int kColSource = 1;
constexpr int kColStatus = 2;
}  // namespace

PayloadPage::PayloadPage(Wizard& wizard, QWidget* parent)
    : QWizardPage(parent), wizard_(wizard) {

    auto* lay = PageChrome::install(
        this, QStringLiteral("Building the install payload"),
        QStringLiteral("The answer file, the guest binaries and the drivers that must be "
                       "present on the very first boot."));
    table_ = new QTableWidget(3, 3);
    ui::Theme::rows_table(table_, QStringList{QStringLiteral("ARTIFACT"),
                                              QStringLiteral("SOURCE"),
                                              QStringLiteral("STATE")});
    table_->horizontalHeader()->setSectionResizeMode(kColTask, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColSource, QHeaderView::Fixed);
    table_->setColumnWidth(kColSource, 220);
    // Fixed: the state is a cell widget, and a column sized to its
    // items measures the empty item behind it and clips the pill.
    table_->horizontalHeader()->setSectionResizeMode(kColStatus, QHeaderView::Fixed);
    table_->setColumnWidth(kColStatus, 190);
    // The artifacts, named as themselves. A row says what is being made
    // or fetched; the verb is in the state column, where it changes.
    table_->setItem(0, kColTask, new QTableWidgetItem("virtio-win.iso"));
    // Its own row because it can fail on its own without stopping the
    // install — and because a missing shared-memory device is otherwise
    // only discovered much later, as a PCI device that is not there.
    table_->setItem(1, kColTask, new QTableWidgetItem("ivshmem driver"));
    table_->setItem(2, kColTask, new QTableWidgetItem("lsw-payload.iso"));

    // Where each one comes from. The shared-memory driver is the one
    // whose source is worth naming: it is not in Red Hat's virtio-win
    // media and has to be taken out of the Looking Glass host package.
    table_->setItem(0, kColSource, ui::Theme::kind_cell("fedorapeople"));
    table_->setItem(1, kColSource, ui::Theme::kind_cell("Looking Glass host package"));
    table_->setItem(2, kColSource, ui::Theme::kind_cell("local build"));
    ui::Theme::fit_rows(table_);

    // Why one of those rows exists at all. Static, and separate from
    // the status line, which the pipeline overwrites.
    auto* footnote = new QLabel(QStringLiteral(
        "The shared memory driver is injected offline, because a driver delivered over "
        "the channel it provides cannot bootstrap that channel."));
    footnote->setObjectName(QString::fromLatin1(lsw::host::ui::Theme::Role::kMeta));
    footnote->setWordWrap(true);

    status_label_ = new QLabel;
    status_label_->setObjectName(QString::fromLatin1(lsw::host::ui::Theme::Role::kMeta));
    status_label_->setWordWrap(true);

    lay->addWidget(table_);
    lay->addWidget(footnote);
    lay->addWidget(status_label_);
    lay->addStretch(1);
}

void PayloadPage::initializePage() {
    complete_ = false;
    set_row_status(0, RowStatus::Pending);
    set_row_status(1, RowStatus::Pending);
    set_row_status(2, RowStatus::Pending);
    Q_EMIT completeChanged();

    auto* p = wizard_.pipeline();
    if (p == nullptr) {
        status_label_->setText("Internal error: pipeline not started.");
        return;
    }
    using Step = ::lsw::host::provision::Pipeline::Step;

    connect(p, &::lsw::host::provision::Pipeline::payload_step_started, this,
            [this](int row) { set_row_status(row, RowStatus::Running); });
    connect(p, &::lsw::host::provision::Pipeline::payload_step_progress, this,
            [this](int row, const QString& detail) {
                set_row_status(row, RowStatus::Running, detail);
            });
    connect(p, &::lsw::host::provision::Pipeline::payload_step_finished, this,
            [this](int row, bool ok, const QString& detail) {
                set_row_status(row, ok ? RowStatus::Done : RowStatus::Failed, detail);
            });
    connect(p, &::lsw::host::provision::Pipeline::step_started, this,
            [this](Step s, const QString& desc) {
                if (s == Step::BuildingPayloadIso) status_label_->setText(desc);
            });
    connect(p, &::lsw::host::provision::Pipeline::step_finished, this, [this](Step s) {
        if (s != Step::BuildingPayloadIso) return;
        status_label_->setText("Payload ready.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) w->next();
        });
    });
    connect(p, &::lsw::host::provision::Pipeline::failed, this,
            [this](Step s, const QString& err) {
                if (s != Step::BuildingPayloadIso) return;
                status_label_->setText(QStringLiteral("Payload step failed: %1").arg(err));
            });
}

void PayloadPage::demo_fill() {
    set_row_status(0, RowStatus::Running, QStringLiteral("877 MiB, 71 %"));
    set_row_status(1, RowStatus::Pending);
    set_row_status(2, RowStatus::Pending);
    status_label_->setText(QStringLiteral("Downloading the virtio-win media."));
}

bool PayloadPage::isComplete() const {
    return complete_;
}

int PayloadPage::nextId() const {
    return Wizard::Install;
}

void PayloadPage::set_row_status(int row, RowStatus s, const QString& detail) {
    if (table_ == nullptr || row < 0 || row >= table_->rowCount()) {
        return;
    }
    QString text;
    const char* role = ui::Theme::Role::kPill;
    switch (s) {
    case RowStatus::Pending:
        text = QStringLiteral("Pending");
        break;
    case RowStatus::Running:
        text = detail.isEmpty() ? QStringLiteral("Running") : detail;
        role = ui::Theme::Role::kPillRun;
        break;
    case RowStatus::Done:
        text = detail.isEmpty() ? QStringLiteral("Done") : detail;
        role = ui::Theme::Role::kPillDone;
        break;
    case RowStatus::Failed:
        text = detail.isEmpty() ? QStringLiteral("Failed")
                                : QStringLiteral("Failed: %1").arg(detail);
        role = ui::Theme::Role::kPillFail;
        break;
    }
    auto* holder = new QWidget;
    auto* layout = new QHBoxLayout(holder);
    layout->setContentsMargins(8, 0, 8, 0);
    auto* pill = new QLabel(QStringLiteral("\u25cf  %1").arg(text));
    pill->setObjectName(QString::fromLatin1(role));
    layout->addWidget(pill, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch(1);
    table_->setCellWidget(row, kColStatus, holder);
}

}  // namespace lsw::host::wizard
