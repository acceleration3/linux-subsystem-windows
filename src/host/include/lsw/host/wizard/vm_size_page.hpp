#pragma once

#include "lsw/host/provision/install/wim_edition_lister.hpp"
#include "lsw/host/wizard/wizard_page.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QVector>

namespace lsw::host::ui { class SliderSpin; }

namespace lsw::host::wizard {

class Wizard;

class VmSizePage : public WizardPage {
    Q_OBJECT
public:
    explicit VmSizePage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    bool validatePage() override;
    [[nodiscard]] int nextId() const override;

    void demo_fill();

private Q_SLOTS:
    void on_editions_ready();
    void on_languages_ready();

private:
    QFormLayout* form_ = nullptr;
    QLabel* footnote_ = nullptr;

    void refresh_footnote();
    void show_reading();
    Wizard& wizard_;
    QLineEdit* user_edit_ = nullptr;
    QLineEdit* pass_edit_ = nullptr;
    QLineEdit* computer_edit_ = nullptr;
    ui::SliderSpin* cpu_slider_ = nullptr;
    ui::SliderSpin* mem_slider_ = nullptr;
    ui::SliderSpin* disk_slider_ = nullptr;
    QComboBox* edition_combo_ = nullptr;
    QLabel* edition_status_ = nullptr;
    QComboBox* language_combo_ = nullptr;
    QLabel* language_status_ = nullptr;

    QFutureWatcher<QVector<provision::install::WimEdition>>* edition_watcher_ = nullptr;
    QFutureWatcher<QStringList>* language_watcher_ = nullptr;
    QString probed_iso_;
    bool editions_loaded_ = false;
    bool languages_loaded_ = false;
};

}
