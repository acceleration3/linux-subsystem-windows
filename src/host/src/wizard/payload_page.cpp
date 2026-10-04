#include "lsw/host/wizard/payload_page.hpp"

#include "lsw/host/provision/pipeline.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::wizard {

namespace {

using RowStatus = ui::StatusPill::State;

constexpr int kColTask = 0;
constexpr int kColSource = 1;
constexpr int kColStatus = 2;

// The pipeline reports rows 0 to 2; the driver package row follows the
// payload ISO it is staged onto.
constexpr int kRowPayloadIso = 2;
constexpr int kRowAvgpu = 3;

}  // namespace

PayloadPage::PayloadPage(Wizard& wizard, QWidget* parent)
    : PipelinePage(parent), wizard_(wizard) {

    auto* lay = install_chrome(
        QStringLiteral("Building the install payload"),
        QStringLiteral("The answer file, the guest binaries and the drivers that must be "
                       "present on the very first boot."));
    table_ = new QTableWidget(4, 3);
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
    // The guest's display driver, x64 and the x86 user-mode drivers
    // 32-bit processes load. It is staged onto the payload ISO, so it is
    // ready when that is, and Windows installs it before the first logon.
    table_->setItem(kRowAvgpu, kColTask,
                    new QTableWidgetItem("avgpu driver package, x64 and x86"));

    // Where each one comes from. The shared-memory driver is the one
    // whose source is worth naming: it is not in Red Hat's virtio-win
    // media and has to be taken out of the Looking Glass host package.
    table_->setItem(0, kColSource, ui::Theme::kind_cell("fedorapeople"));
    table_->setItem(1, kColSource, ui::Theme::kind_cell("Looking Glass host package"));
    table_->setItem(2, kColSource, ui::Theme::kind_cell("local build"));
    table_->setItem(kRowAvgpu, kColSource, ui::Theme::kind_cell("accel-virt, local build"));
    ui::Theme::fit_rows(table_);

    // Why one of those rows exists at all. Static, and separate from
    // the status line, which the pipeline overwrites.
    auto* footnote = ui::Widgets::meta(
        QStringLiteral("The shared memory driver is injected offline, because a driver "
                       "delivered over the channel it provides cannot bootstrap that channel."),
        true);

    status_label_ = ui::Widgets::meta(QString{}, true);

    lay->addWidget(table_);
    lay->addWidget(footnote);
    lay->addWidget(status_label_);
    lay->addStretch(1);
}

void PayloadPage::initializePage() {
    for (int row = 0; row < table_->rowCount(); ++row) {
        set_row_status(row, RowStatus::Pending);
    }
    reset_complete();

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
        finish_and_advance();
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
    set_row_status(kRowAvgpu, RowStatus::Pending);
    status_label_->setText(QStringLiteral("Downloading the virtio-win media."));
}

int PayloadPage::nextId() const {
    return Wizard::Install;
}

void PayloadPage::set_row_status(int row, RowStatus s, const QString& detail) {
    ui::StatusPill::set_cell(table_, row, kColStatus, s, detail);
    // The driver package travels on the payload ISO: its row says what
    // the ISO's does.
    if (row == kRowPayloadIso) {
        ui::StatusPill::set_cell(table_, kRowAvgpu, kColStatus, s, detail);
    }
}

}  // namespace lsw::host::wizard
