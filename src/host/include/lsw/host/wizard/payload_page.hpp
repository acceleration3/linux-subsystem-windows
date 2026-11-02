#pragma once

#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/wizard/pipeline_page.hpp"

#include <QLabel>
#include <QTableWidget>

namespace lsw::host::wizard {

class Wizard;

class PayloadPage : public PipelinePage {
    Q_OBJECT
public:
    explicit PayloadPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

    void demo_fill();
    [[nodiscard]] int nextId() const override;

private:
    void set_row_status(int row, ui::StatusPill::State s, const QString& detail = {});

    Wizard& wizard_;
    QTableWidget* table_ = nullptr;
    QLabel* status_label_ = nullptr;
};

}
