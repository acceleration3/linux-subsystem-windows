#pragma once

#include "lsw/host/provision/install/wim_edition_lister.hpp"
#include "lsw/host/wizard/wizard_page.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QVector>

namespace lsw::host::ui { class SliderSpin; }

namespace lsw::host::wizard {

class Wizard;

// Step 5: VM sizing + local Windows account credentials + Windows
// edition. Sliders for CPU/memory/disk (bounded by host caps), the
// local-account username/password baked into autounattend.xml, and an
// edition combo box populated asynchronously by reading the chosen
// ISO's WIM image table (provision::install::WimEditionLister::list, run off-thread
// because it shells out to 7z). The VM name is fixed (config::kVmName).
class VmSizePage : public WizardPage {
    Q_OBJECT
public:
    explicit VmSizePage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    bool validatePage() override;
    [[nodiscard]] int nextId() const override;

private Q_SLOTS:
    void on_editions_ready();

private:
    QFormLayout* form_ = nullptr;
    QLabel* footnote_ = nullptr;

    void refresh_footnote();
    Wizard& wizard_;
    QLineEdit* user_edit_ = nullptr;
    QLineEdit* pass_edit_ = nullptr;
    QLineEdit* computer_edit_ = nullptr;
    ui::SliderSpin* cpu_slider_ = nullptr;
    ui::SliderSpin* mem_slider_ = nullptr;
    ui::SliderSpin* disk_slider_ = nullptr;
    QComboBox* edition_combo_ = nullptr;
    QLabel* edition_status_ = nullptr;

    // Async WIM-edition probe. ``probed_iso_`` guards against
    // re-probing the same ISO every time the page is revisited.
    QFutureWatcher<QVector<provision::install::WimEdition>>* edition_watcher_ = nullptr;
    QString probed_iso_;
    bool editions_loaded_ = false;
};

}  // namespace lsw::host::wizard
