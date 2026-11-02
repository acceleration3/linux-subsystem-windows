#pragma once

#include "lsw/host/provision/setup_plan_parser.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/wizard/pipeline_page.hpp"

#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QVector>

namespace lsw::host::wizard {

class Wizard;

class SetupPage : public PipelinePage {
    Q_OBJECT
public:
    explicit SetupPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

    void demo_fill();
    [[nodiscard]] int nextId() const override;

private:
    void set_row_status(int row, ui::StatusPill::State s, const QString& detail = {});
    void populate_from_plan(const QVector<::lsw::host::provision::SetupStage>& stages);

    Wizard& wizard_;
    QTableWidget* table_ = nullptr;
    QLabel* stage_label_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_label_ = nullptr;
    int total_steps_ = 0;
    int finished_steps_ = 0;
    QVector<int> stage_sizes_;
    void show_stage(int row);
};

}
