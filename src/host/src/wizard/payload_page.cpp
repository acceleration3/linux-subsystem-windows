#include "lsw/host/wizard/payload_page.hpp"

#include "lsw/host/wizard/wizard.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

namespace {
constexpr int kColTask = 0;
constexpr int kColStatus = 1;
}  // namespace

PayloadPage::PayloadPage(Wizard& wizard, QWidget* parent)
    : QWizardPage(parent), wizard_(wizard) {
    setTitle("Preparing the install payload");
    setSubTitle("Fetching the latest virtio-win drivers and building the "
                "LSW payload ISO.");

    auto* lay = new QVBoxLayout(this);
    table_ = new QTableWidget(3, 2);
    table_->setHorizontalHeaderLabels(QStringList{"TASK", "STATUS"});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSectionResizeMode(kColTask, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColStatus, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setItem(0, kColTask, new QTableWidgetItem("Download virtio-win.iso"));
    // Its own row because it can fail on its own without stopping the
    // install — and because a missing shared-memory device is otherwise
    // only discovered much later, as a PCI device that is not there.
    table_->setItem(1, kColTask, new QTableWidgetItem("Fetch IVSHMEM driver"));
    table_->setItem(2, kColTask, new QTableWidgetItem("Build LSW payload ISO"));

    status_label_ = new QLabel("Preparing the install payload...");
    status_label_->setWordWrap(true);

    lay->addWidget(table_, /*stretch=*/1);
    lay->addWidget(status_label_);
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
    QColor fg;
    switch (s) {
    case RowStatus::Pending:
        text = QStringLiteral("Pending");
        fg = QColor("#888888");
        break;
    case RowStatus::Running:
        text = detail.isEmpty() ? QStringLiteral("Running...") : detail;
        fg = QColor("#1f6feb");
        break;
    case RowStatus::Done:
        text = detail.isEmpty() ? QStringLiteral("Done") : detail;
        fg = QColor("#1a7f37");
        break;
    case RowStatus::Failed:
        text = detail.isEmpty() ? QStringLiteral("Failed")
                                 : QStringLiteral("Failed: %1").arg(detail);
        fg = QColor("#cf222e");
        break;
    }
    auto* item = new QTableWidgetItem(text);
    item->setForeground(QBrush(fg));
    table_->setItem(row, kColStatus, item);
}

}  // namespace lsw::host::wizard
