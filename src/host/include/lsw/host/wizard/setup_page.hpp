#pragma once

#include "lsw/host/provision/setup_plan_parser.hpp"

#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QWizardPage>

namespace lsw::host::wizard {

class Wizard;

// Renders setup-step progress and the virtio-swap phase. Pure
// presentation: subscribes to the wizard's Pipeline for the setup
// plan, per-step status, and the post-setup virtio-swap progress.
// Auto-advances when the pipeline reaches the WaitingForAgentAfterSwap
// terminal state.
class SetupPage : public QWizardPage {
    Q_OBJECT
public:
    explicit SetupPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

    // Fill the table with a plausible run. Used by --screenshot, where
    // there is no pipeline and an empty table says nothing about
    // whether the page matches the design.
    void demo_fill();
    [[nodiscard]] bool isComplete() const override;
    [[nodiscard]] int nextId() const override;

private:
    enum class RowStatus { Pending, Running, Done, Failed };
    void set_row_status(int row, RowStatus s, const QString& detail = {});
    void populate_from_plan(const QVector<::lsw::host::provision::SetupStage>& stages);

    Wizard& wizard_;
    QTableWidget* table_ = nullptr;
    QLabel* stage_label_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_label_ = nullptr;
    int total_steps_ = 0;
    int finished_steps_ = 0;
    bool complete_ = false;
};

}  // namespace lsw::host::wizard
