#pragma once

#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/wizard/pipeline_page.hpp"

#include <QLabel>
#include <QTableWidget>

namespace lsw::host::wizard {

class Wizard;

// SetupPage-style view of the BuildingPayloadIso stage. Two fixed
// rows, lsw-payload.iso and the avgpu driver package staged onto it,
// driven by the pipeline's payload_step_* signals.
// Auto-advances to Install when the stage finishes.
class PayloadPage : public PipelinePage {
    Q_OBJECT
public:
    explicit PayloadPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

    // A plausible mid-run state for --screenshot, where there is no
    // pipeline to drive the rows.
    void demo_fill();
    [[nodiscard]] int nextId() const override;

private:
    void set_row_status(int row, ui::StatusPill::State s, const QString& detail = {});

    Wizard& wizard_;
    QTableWidget* table_ = nullptr;
    QLabel* status_label_ = nullptr;
};

}  // namespace lsw::host::wizard
