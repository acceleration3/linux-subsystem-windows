#include "lsw/host/wizard/setup_page.hpp"
#include "lsw/host/wizard/page_chrome.hpp"

#include "lsw/host/wizard/wizard.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QBrush>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QColor>
#include <QHeaderView>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

namespace {
constexpr int kColName = 0;
constexpr int kColKind = 1;
constexpr int kColStatus = 2;

[[nodiscard]] QString kind_label(::lsw::host::provision::StepKind k) {
    using K = ::lsw::host::provision::StepKind;
    switch (k) {
    case K::DownloadInstall: return QStringLiteral("download + install");
    case K::RunScript:       return QStringLiteral("run script");
    case K::DownloadExtract: return QStringLiteral("download + extract");
    }
    return {};
}
}  // namespace

SetupPage::SetupPage(Wizard& wizard, QWidget* parent) : QWizardPage(parent), wizard_(wizard) {

    auto* lay = PageChrome::install(
        this, QStringLiteral("Installing runtimes inside the guest"),
        QStringLiteral("Reported live by the agent. Steps inside one stage run in "
                       "parallel."));

    // Which stage the table is showing, and whether its steps run at
    // the same time. The square is the design's: a stage is a group,
    // and a group deserves a mark that is not another word.
    auto* stage_row = new QWidget;
    auto* stage_layout = new QHBoxLayout(stage_row);
    stage_layout->setContentsMargins(0, 0, 0, 0);
    stage_layout->setSpacing(8);
    auto* dot = new QFrame;
    dot->setFixedSize(7, 7);
    dot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 2px;")
                           .arg(QString::fromLatin1(ui::Theme::Color::kAmber)));
    stage_layout->addWidget(dot);
    stage_label_ = new QLabel(QStringLiteral("READING THE PLAN"));
    stage_label_->setObjectName(QString::fromLatin1(ui::Theme::Role::kCaption));
    stage_layout->addWidget(stage_label_);
    stage_layout->addStretch(1);
    lay->addWidget(stage_row);

    table_ = new QTableWidget(0, 3);
    ui::Theme::rows_table(table_, QStringList{QStringLiteral("STEP"),
                                              QStringLiteral("TYPE"),
                                              QStringLiteral("STATUS")});
    table_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColKind, QHeaderView::Fixed);
    table_->setColumnWidth(kColKind, 170);
    // Fixed: a column sized to its items measures the empty item behind
    // the pill widget and clips it.
    table_->horizontalHeader()->setSectionResizeMode(kColStatus, QHeaderView::Fixed);
    table_->setColumnWidth(kColStatus, 210);
    ui::Theme::fit_rows(table_);
    lay->addWidget(table_);
    lay->addStretch(1);

    // What is left, and what happens after it. Both belong at the foot
    // of the page: they describe the whole run rather than a row.
    progress_ = new QProgressBar;
    progress_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMeter));
    progress_->setProperty("pressure", QStringLiteral("hot"));
    progress_->setTextVisible(false);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    lay->addWidget(progress_);

    status_label_ = new QLabel(QStringLiteral(
        "Then the VM shuts down, restarts on the virtio configuration, and the agent "
        "reconnects."));
    status_label_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMeta));
    status_label_->setWordWrap(true);
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
    using Step = ::lsw::host::provision::Pipeline::Step;
    using StepKind = ::lsw::host::provision::StepKind;
    using Stage = ::lsw::host::provision::SetupStage;

    connect(p, &::lsw::host::provision::Pipeline::setup_plan_ready, this,
            [this](const QVector<Stage>& stages) { populate_from_plan(stages); });
    // If the pipeline already parsed the plan before we connected
    // (page shown late), pull it now so the table still populates.
    if (!p->setup_plan().isEmpty()) {
        populate_from_plan(p->setup_plan());
    }
    connect(p, &::lsw::host::provision::Pipeline::setup_step_started, this,
            [this](int row) { set_row_status(row, RowStatus::Running); });
    connect(p, &::lsw::host::provision::Pipeline::setup_step_progress, this,
            [this](int row, const QString& detail) {
                set_row_status(row, RowStatus::Running, detail);
            });
    connect(p, &::lsw::host::provision::Pipeline::setup_step_finished, this,
            [this](int row, bool ok, const QString& detail) {
                set_row_status(row, ok ? RowStatus::Done : RowStatus::Failed, detail);
                if (total_steps_ > 0) {
                    ++finished_steps_;
                    progress_->setValue(100 * finished_steps_ / total_steps_);
                }
            });
    connect(p, &::lsw::host::provision::Pipeline::step_started, this,
            [this](Step s, const QString& desc) {
                if (s == Step::ShuttingDown || s == Step::StartingVmVirtio
                    || s == Step::WaitingForAgentAfterSwap) {
                    status_label_->setText(desc);
                }
            });
    connect(p, &::lsw::host::provision::Pipeline::failed, this,
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
    connect(p, &::lsw::host::provision::Pipeline::completed, this, [this](const QUuid&) {
        progress_->setValue(100);
        status_label_->setText("Setup is complete. The VM is on the virtio configuration.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) w->next();
        });
    });
    (void)StepKind{};  // silence unused-using if the compiler is strict
}

void SetupPage::demo_fill() {
    using ::lsw::host::provision::SetupStage;
    using ::lsw::host::provision::StepKind;
    SetupStage stage;
    stage.steps.push_back({QStringLiteral("DXVK 2.7.1"), StepKind::DownloadExtract});
    stage.steps.push_back({QStringLiteral("VKD3D-Proton 3.0.1"), StepKind::DownloadExtract});
    stage.steps.push_back({QStringLiteral("Tune Windows for VM use"), StepKind::RunScript});
    populate_from_plan({stage});
    set_row_status(0, RowStatus::Done, QStringLiteral("Done, 12.4 MiB"));
    set_row_status(1, RowStatus::Running, QStringLiteral("Extracting"));
    set_row_status(2, RowStatus::Running, QStringLiteral("Applying registry"));
    progress_->setValue(55);
    status_label_->setText(QStringLiteral(
        "Then the VM shuts down, restarts on the virtio configuration, and the agent "
        "reconnects."));
}

bool SetupPage::isComplete() const {
    return complete_;
}

int SetupPage::nextId() const {
    return Wizard::Done;
}

void SetupPage::populate_from_plan(const QVector<::lsw::host::provision::SetupStage>& stages) {
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
            table_->setItem(row, kColKind, ui::Theme::kind_cell(kind_label(s.kind)));
            set_row_status(row, RowStatus::Pending);
            ++row;
        }
    }
    ui::Theme::fit_rows(table_);
    total_steps_ = total;
    finished_steps_ = 0;
    progress_->setValue(0);

    // The caption says where in the plan this is, in the design's own
    // words: a stage, and whether its steps run together.
    if (stages.isEmpty()) {
        stage_label_->setText(QStringLiteral("NOTHING TO DO"));
        status_label_->setText(
            QStringLiteral("No setup steps are configured, so there is nothing to do."));
        return;
    }
    const int first = static_cast<int>(stages.front().steps.size());
    stage_label_->setText(first > 1
                              ? QStringLiteral("STAGE 1 OF %1, %2 STEPS IN PARALLEL")
                                    .arg(stages.size())
                                    .arg(first)
                              : QStringLiteral("STAGE 1 OF %1").arg(stages.size()));
    if (parallel_groups == 0) {
        stage_label_->setText(QStringLiteral("%1 STEPS, ONE AT A TIME").arg(total));
    }
}

void SetupPage::set_row_status(int row, RowStatus s, const QString& detail) {
    if (row < 0 || row >= table_->rowCount()) {
        return;
    }
    // A pill rather than coloured text: the state reads from its shape
    // and its dot before the word is read, which is what a column of
    // them is for.
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
