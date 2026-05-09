#pragma once

#include <QLabel>
#include <QTableWidget>
#include <QWizardPage>

namespace lsw::manager::ui::wizard {

class Wizard;

// SetupPage-style view of the BuildingPayloadIso stage. Two fixed
// task rows — "Download virtio-win.iso" (with live %) and "Build LSW
// payload ISO" — driven by the pipeline's payload_step_* signals.
// Auto-advances to Install when the stage finishes.
class PayloadPage : public QWizardPage {
    Q_OBJECT
public:
    explicit PayloadPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    [[nodiscard]] int nextId() const override;

private:
    enum class RowStatus { Pending, Running, Done, Failed };
    void set_row_status(int row, RowStatus s, const QString& detail = {});

    Wizard& wizard_;
    QTableWidget* table_ = nullptr;
    QLabel* status_label_ = nullptr;
    bool complete_ = false;
};

}  // namespace lsw::manager::ui::wizard
