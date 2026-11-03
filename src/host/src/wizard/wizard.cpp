#include "lsw/host/wizard/wizard.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/wizard/done_page.hpp"
#include "lsw/host/wizard/install_page.hpp"
#include "lsw/host/wizard/iso_choice_page.hpp"
#include "lsw/host/wizard/license_page.hpp"
#include "lsw/host/wizard/payload_page.hpp"
#include "lsw/host/wizard/setup_page.hpp"
#include "lsw/host/wizard/vm_size_page.hpp"
#include "lsw/host/wizard/welcome_page.hpp"

#include "lsw/host/ui/content_height.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStorageInfo>
#include <QThread>

namespace lsw::host::wizard {

Wizard::Wizard(services::HostServices& services, QWidget* parent) : QWizard(parent), services_(services) {
    setWindowTitle("LSW first-run wizard");
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::IndependentPages, false);
    setOption(QWizard::NoCancelButtonOnLastPage);
    resize(880, 620);
    ui::ContentHeight::keep(this, [this] { return currentPage(); });

    detect_host_caps();

    setPage(Welcome, new WelcomePage);
    setPage(License, new LicensePage);
    setPage(IsoChoice, new IsoChoicePage(*this));
    setPage(VmSize, new VmSizePage(*this));
    setPage(Payload, new PayloadPage(*this));
    setPage(Install, new InstallPage(*this));
    setPage(Setup, new SetupPage(*this));
    setPage(Done, new DonePage(*this));
    setStartId(Welcome);

    setButtonLayout({});
    setOption(NoCancelButton, true);
    setOption(NoBackButtonOnStartPage, true);

    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        highlight_step(id);
    });
}

const QStringList& Wizard::step_names() {
    static const QStringList names = {
        QStringLiteral("Welcome"),   QStringLiteral("Licensing"),
        QStringLiteral("Windows ISO"), QStringLiteral("Size and account"),
        QStringLiteral("Payload"),   QStringLiteral("Installing"),
        QStringLiteral("Setup steps"), QStringLiteral("Finished"),
    };
    return names;
}

void Wizard::highlight_step(int page_id) {
    auto* shown = qobject_cast<WizardPage*>(currentPage());
    if (shown == nullptr) {
        return;
    }
    const PageChrome& chrome = shown->chrome();
    auto* steps = chrome.steps;
    if (steps == nullptr) {
        return;
    }
    if (steps->count() == 0) {
        for (const auto& name : step_names()) {
            steps->addItem(name);
        }
        steps->setFixedHeight(static_cast<int>(step_names().size()) * 30);
    }
    if (const qint64 free = QStorageInfo{QDir(services_.settings().disks_dir())}.bytesAvailable();
        free > 0) {
        state_.host_free_disk_bytes = free;
    }
    chrome.rail_host->setText(QStringLiteral("host: %1 threads, %2 GiB")
                                  .arg(state_.host_cpu_threads)
                                  .arg(state_.host_ram_mib / 1024));
    chrome.rail_free->setText(
        QStringLiteral("free: %1").arg(ui::Format::gib(state_.host_free_disk_bytes, 0)));
    for (int row = 0; row < steps->count(); ++row) {
        auto* item = steps->item(row);
        const bool done = row < page_id;
        const bool current = row == page_id;
        const QString mark = done ? QStringLiteral("\u2713")
                                  : QStringLiteral("%1").arg(row + 1, 2, 10, QLatin1Char('0'));
        item->setText(QStringLiteral("%1   %2").arg(mark, step_names().at(row)));
        item->setSelected(current);
        auto font = item->font();
        font.setWeight(current ? QFont::Medium : QFont::Normal);
        item->setFont(font);
        item->setForeground(ui::Theme::color(
            done ? ui::Theme::Color::kFgDim
                 : (current ? ui::Theme::Color::kFg : ui::Theme::Color::kFgFaint)));
    }
    steps->setCurrentRow(page_id);

    const bool locked = page_id == Payload || page_id == Install || page_id == Setup;
    chrome.counter->setText((locked ? QStringLiteral("step %1 of %2, cannot go back")
                                    : QStringLiteral("step %1 of %2"))
                                .arg(page_id + 1)
                                .arg(steps->count()));
    chrome.back->setVisible(!locked);
    chrome.back->setEnabled(page_id != Welcome);
    chrome.next->setVisible(!locked);
    chrome.next->setEnabled(shown->isComplete());
    chrome.next->setText(page_id == Done ? QStringLiteral("Open the manager")
                                         : QStringLiteral("Next"));
    chrome.cancel->setVisible(locked);
    chrome.cancel->setText(page_id == Install   ? QStringLiteral("Cancel install")
                           : page_id == Payload ? QStringLiteral("Cancel")
                                                : QStringLiteral("Cancel setup"));
}

void Wizard::detect_host_caps() {
    state_.host_cpu_threads = std::max(1, QThread::idealThreadCount());

    QFile mi{"/proc/meminfo"};
    if (mi.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto text = QString::fromLatin1(mi.readAll());
        const auto idx = text.indexOf("MemTotal:");
        if (idx >= 0) {
            const auto rest = text.mid(idx);
            const auto kb_str = rest.split(' ', Qt::SkipEmptyParts).value(1);
            const auto kb = kb_str.toLongLong();
            if (kb > 0) {
                state_.host_ram_mib = static_cast<int>(kb / 1024);
            }
        }
    }

    QStorageInfo info{QDir(services_.settings().disks_dir())};
    state_.host_free_disk_bytes = info.bytesAvailable();
}

void Wizard::start_pipeline() {
    if (pipeline_ != nullptr) {
        return;
    }
    pipeline_ = new ::lsw::host::provision::Pipeline(services_, this);
    state_.failure.clear();
    connect(pipeline_, &::lsw::host::provision::Pipeline::failed, this,
            [this](::lsw::host::provision::Pipeline::Step, const QString& error) {
                state_.failure = error;
            });
    ::lsw::host::provision::PipelineInputs in;
    in.iso_local_path = state_.chosen_iso_path;
    in.iso_download_url = state_.fido_download_url;
    in.iso_display_label = state_.fido_edition_label;
    in.user_name = state_.user_name;
    in.user_password = state_.user_password;
    in.computer_name = state_.computer_name;
    in.sizing = state_.draft.sizing;
    in.image_index = state_.image_index;
    in.edition_id = state_.edition_id;
    in.language = state_.language;
    in.debloat = state_.debloat;
    in.image_already_trimmed = state_.iso_prepared;
    pipeline_->set_inputs(in);
    pipeline_->start();
}

void Wizard::accept() {
    QWizard::accept();
    Q_EMIT complete();
}

}
