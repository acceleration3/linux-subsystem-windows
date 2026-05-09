#include "lsw/manager/ui/wizard/setup_page.hpp"

#include "lsw/manager/ui/wizard/wizard.hpp"
#include "lsw/manager/wizard/pipeline.hpp"

#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::manager::ui::wizard {

namespace {
constexpr int kColName = 0;
constexpr int kColKind = 1;
constexpr int kColStatus = 2;

[[nodiscard]] QString kind_label(::lsw::manager::wizard::StepKind k) {
    using K = ::lsw::manager::wizard::StepKind;
    switch (k) {
    case K::DownloadInstall: return QStringLiteral("Download + install");
    case K::RunScript:       return QStringLiteral("Run script");
    case K::DownloadExtract: return QStringLiteral("Download + extract");
    }
    return {};
}
}  // namespace

SetupPage::SetupPage(Wizard& wizard, QWidget* parent) : QWizardPage(parent), wizard_(wizard) {
    setTitle("Setting up the VM");
    setSubTitle("Installing third-party runtimes and applying VM tuning. "
                "Progress for each step is reported live by the guest agent. "
                "Steps inside a nested array in setup-steps.json run in parallel.");

    auto* lay = new QVBoxLayout(this);
    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels(QStringList{"Step", "Type", "Status"});
    table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColKind, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColStatus, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);

    status_label_ = new QLabel("Loading steps...");
    status_label_->setWordWrap(true);

    lay->addWidget(table_, /*stretch=*/1);
    lay->addWidget(status_label_);
}

void SetupPage::initializePage() {
    complete_ = false;
    Q_EMIT completeChanged();

    auto* p = wizard_.pipeline();
    if (p == nullptr) {
        status_label_->setText("Internal error: pipeline not started.");
        return;
    }
    using Step = ::lsw::manager::wizard::Pipeline::Step;
    using StepKind = ::lsw::manager::wizard::StepKind;
    using Stage = ::lsw::manager::wizard::SetupStage;

    connect(p, &::lsw::manager::wizard::Pipeline::setup_plan_ready, this,
            [this](const QVector<Stage>& stages) { populate_from_plan(stages); });
    // If the pipeline already parsed the plan before we connected
    // (page shown late), pull it now so the table still populates.
    if (!p->setup_plan().isEmpty()) {
        populate_from_plan(p->setup_plan());
    }
    connect(p, &::lsw::manager::wizard::Pipeline::setup_step_started, this,
            [this](int row) { set_row_status(row, RowStatus::Running); });
    connect(p, &::lsw::manager::wizard::Pipeline::setup_step_progress, this,
            [this](int row, const QString& detail) {
                set_row_status(row, RowStatus::Running, detail);
            });
    connect(p, &::lsw::manager::wizard::Pipeline::setup_step_finished, this,
            [this](int row, bool ok, const QString& detail) {
                set_row_status(row, ok ? RowStatus::Done : RowStatus::Failed, detail);
            });
    connect(p, &::lsw::manager::wizard::Pipeline::step_started, this,
            [this](Step s, const QString& desc) {
                if (s == Step::ShuttingDown || s == Step::StartingVmVirtio
                    || s == Step::WaitingForAgentAfterSwap) {
                    status_label_->setText(desc);
                }
            });
    connect(p, &::lsw::manager::wizard::Pipeline::failed, this,
            [this](Step s, const QString& err) {
                if (s == Step::RunningSetup || s == Step::ShuttingDown
                    || s == Step::StartingVmVirtio || s == Step::WaitingForAgentAfterSwap) {
                    status_label_->setText(QStringLiteral("Setup failed: %1").arg(err));
                    // Terminal — let the wizard advance to Done so the user
                    // can read the message and (manually) open the manager.
                    complete_ = true;
                    Q_EMIT completeChanged();
                    QTimer::singleShot(0, this, [this] {
                        if (auto* w = wizard()) w->next();
                    });
                }
            });
    connect(p, &::lsw::manager::wizard::Pipeline::completed, this, [this](const QUuid&) {
        status_label_->setText("Setup complete — VM is on the virtio-optimised config.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) w->next();
        });
    });
    (void)StepKind{};  // silence unused-using if the compiler is strict
}

bool SetupPage::isComplete() const {
    return complete_;
}

int SetupPage::nextId() const {
    return Wizard::Done;
}

void SetupPage::populate_from_plan(const QVector<::lsw::manager::wizard::SetupStage>& stages) {
    int total = 0;
    int parallel_groups = 0;
    for (const auto& st : stages) {
        total += static_cast<int>(st.steps.size());
        if (st.steps.size() > 1) ++parallel_groups;
    }
    table_->setRowCount(total);
    int row = 0;
    for (const auto& st : stages) {
        for (const auto& s : st.steps) {
            table_->setItem(row, kColName, new QTableWidgetItem(s.name));
            table_->setItem(row, kColKind, new QTableWidgetItem(kind_label(s.kind)));
            set_row_status(row, RowStatus::Pending);
            ++row;
        }
    }
    if (stages.isEmpty()) {
        status_label_->setText("No setup steps configured — nothing to do.");
    } else if (parallel_groups > 0) {
        status_label_->setText(QStringLiteral("%1 step(s) across %2 stage(s); %3 parallel group(s).")
                                   .arg(total).arg(stages.size()).arg(parallel_groups));
    } else {
        status_label_->setText(QStringLiteral("%1 step(s) queued.").arg(total));
    }
}

void SetupPage::set_row_status(int row, RowStatus s, const QString& detail) {
    if (row < 0 || row >= table_->rowCount()) {
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

}  // namespace lsw::manager::ui::wizard
