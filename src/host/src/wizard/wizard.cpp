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

#include "lsw/host/ui/theme.hpp"

#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>

namespace lsw::host::wizard {

Wizard::Wizard(services::HostServices& services, QWidget* parent) : QWizard(parent), services_(services) {
    setWindowTitle("LSW first-run wizard");
    // Classic rather than Modern. Modern paints a header band of its
    // own, from a palette role the stylesheet cannot reach, so the one
    // light strip in the whole application was the wizard's title.
    // Classic draws the same title and subtitle on the page's own
    // ground, which is the ground everything else already uses.
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::IndependentPages, false);
    setOption(QWizard::NoCancelButtonOnLastPage);
    resize(880, 620);

    detect_host_caps();

    setPage(Welcome, new WelcomePage);
    setPage(License, new LicensePage);
    setPage(IsoChoice, new IsoChoicePage(*this));
    setPage(VmSize, new VmSizePage(*this));
    setPage(Payload, new PayloadPage(*this));
    setPage(Install, new InstallPage(*this));
    setPage(Setup, new SetupPage(*this));
    setPage(Done, new DonePage);
    setStartId(Welcome);

    build_side_rail();

    // Hide Back+Next on the long-running pages. They auto-advance via
    // ``next()`` once the pipeline finishes their phase, so the user has
    // nothing useful to click; a stray Back during Install would tear
    // the VM down mid-Setup.
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        const bool hide_nav = (id == Payload || id == Install || id == Setup);
        if (auto* b = button(BackButton)) b->setVisible(!hide_nav);
        if (auto* b = button(NextButton)) b->setVisible(!hide_nav);
        highlight_step(id);
    });
}

void Wizard::build_side_rail() {
    // QWizard has a slot for exactly this and nothing was in it. The
    // cost is one widget; what it buys is that eight screens read as a
    // sequence with a position in it, rather than eight screens.
    auto* side = new QWidget;
    side->setObjectName(QString::fromLatin1(ui::Theme::Role::kWizardSide));
    side->setFixedWidth(196);

    auto* layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 18, 0, 18);
    layout->setSpacing(0);

    auto* caption = new QLabel(QStringLiteral("SETUP"));
    caption->setObjectName(QString::fromLatin1(ui::Theme::Role::kCaption));
    caption->setContentsMargins(18, 0, 18, 12);
    layout->addWidget(caption);

    steps_ = new QListWidget;
    steps_->setObjectName(QString::fromLatin1(ui::Theme::Role::kWizardSteps));
    // Selection is how the stylesheet marks the current step, so the
    // list keeps it and gives up the mouse instead: the rail reports
    // where you are, it is not a way to jump.
    steps_->setSelectionMode(QAbstractItemView::SingleSelection);
    steps_->setFocusPolicy(Qt::NoFocus);
    steps_->setAttribute(Qt::WA_TransparentForMouseEvents);
    steps_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    steps_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    steps_->setFrameShape(QFrame::NoFrame);
    for (const auto& name : {QStringLiteral("Welcome"), QStringLiteral("Licensing"),
                             QStringLiteral("Windows ISO"),
                             QStringLiteral("Size and account"), QStringLiteral("Payload"),
                             QStringLiteral("Installing"), QStringLiteral("Setup steps"),
                             QStringLiteral("Finished")}) {
        steps_->addItem(name);
    }
    // Sized to the eight it holds. A list left to its own sizeHint gets
    // a scrollbar and clips the last step, which is exactly the thing
    // the rail exists to show.
    steps_->setFixedHeight(8 * 30);
    steps_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(steps_);
    layout->addStretch(1);

    auto* host = new QLabel(QStringLiteral("host: %1 threads, %2 GiB")
                                .arg(state_.host_cpu_threads)
                                .arg(state_.host_ram_mib / 1024));
    host->setObjectName(QString::fromLatin1(ui::Theme::Role::kMeta));
    host->setContentsMargins(18, 10, 18, 0);
    layout->addWidget(host);

    setSideWidget(side);
    highlight_step(Welcome);
}

void Wizard::highlight_step(int page_id) {
    if (steps_ == nullptr) {
        return;
    }
    // The ids are contiguous and in display order, so the page is the
    // row. A tick for what is behind you, the number for what is not.
    for (int row = 0; row < steps_->count(); ++row) {
        auto* item = steps_->item(row);
        const bool done = row < page_id;
        item->setSelected(row == page_id);
        auto font = item->font();
        font.setWeight(row == page_id ? QFont::Medium : QFont::Normal);
        item->setFont(font);
        item->setForeground(QColor(QString::fromLatin1(
            done ? ui::Theme::Color::kFgDim
                 : (row == page_id ? ui::Theme::Color::kFg : ui::Theme::Color::kFgFaint))));
    }
    steps_->setCurrentRow(page_id);
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
    ::lsw::host::provision::PipelineInputs in;
    in.iso_local_path = state_.chosen_iso_path;
    in.iso_download_url = state_.fido_download_url;
    in.iso_display_label = state_.fido_edition_label;
    in.virtio_iso_path = state_.virtio_iso_path;
    in.user_name = state_.user_name;
    in.user_password = state_.user_password;
    in.computer_name = state_.computer_name;
    in.sizing = state_.draft.sizing;
    in.image_index = state_.image_index;
    in.debloat = state_.debloat;
    // Only the trim tool leaves an image that has already been through
    // the preparation pass. Everything else still needs it, because the
    // setup bypasses live in the offline registry and Windows Setup
    // refuses this domain without them; at ``None`` the stager writes
    // those and removes nothing.
    in.image_already_trimmed = state_.iso_prepared;
    pipeline_->set_inputs(in);
    pipeline_->start();
}

void Wizard::accept() {
    QWizard::accept();
    Q_EMIT complete();
}

}  // namespace lsw::host::wizard
