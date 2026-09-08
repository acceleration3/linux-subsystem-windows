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
#include <QPushButton>
#include <QStyle>
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

    // A flat, unclickable custom button is the only thing QWizard will
    // put in its button row that is not a button, and the counter
    // belongs in that row rather than floating in the page.
    setOption(HaveCustomButton1, true);
    if (auto* counter = button(CustomButton1)) {
        // It has to read as the caption it is, not as a fourth button
        // nobody should press. Styled on the widget rather than through
        // the application sheet, because that is the only precedence
        // that beats the QPushButton rules in it.
        if (auto* push = qobject_cast<QPushButton*>(counter)) {
            push->setFlat(true);
        }
        counter->setFocusPolicy(Qt::NoFocus);
        counter->setCursor(Qt::ArrowCursor);
        counter->setStyleSheet(QStringLiteral(
            "QPushButton, QPushButton:hover, QPushButton:pressed,"
            "QPushButton:focus, QPushButton:disabled {"
            "  background: transparent; border: none; outline: none;"
            "  padding: 0px; margin: 0px; min-height: 0px; min-width: 0px;"
            "  font-family: 'JetBrains Mono', 'DejaVu Sans Mono', monospace;"
            "  font-size: 11px; font-weight: 400; color: %1; text-align: left; }")
                                   .arg(QString::fromLatin1(ui::Theme::Color::kFgFaint)));
    }
    setButtonLayout({CustomButton1, Stretch, BackButton, NextButton, FinishButton,
                     CancelButton});
    // Without the angle brackets. They are a convention from a
    // different decade and the rail already says which way is forward.
    setButtonText(BackButton, QStringLiteral("Back"));
    setButtonText(NextButton, QStringLiteral("Next"));
    setButtonText(CancelButton, QStringLiteral("Cancel"));

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

const QStringList& Wizard::step_names() {
    static const QStringList names = {
        QStringLiteral("Welcome"),   QStringLiteral("Licensing"),
        QStringLiteral("Windows ISO"), QStringLiteral("Size and account"),
        QStringLiteral("Payload"),   QStringLiteral("Installing"),
        QStringLiteral("Setup steps"), QStringLiteral("Finished"),
    };
    return names;
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
    for (const auto& name : step_names()) {
        steps_->addItem(name);
    }
    // Sized to the eight it holds. A list left to its own sizeHint gets
    // a scrollbar and clips the last step, which is exactly the thing
    // the rail exists to show.
    steps_->setFixedHeight(step_names().size() * 30);
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
    // row. Each row carries its own number until it is behind you, and
    // then a tick: the rail is a position in a sequence, and a sequence
    // with no numbers is a list.
    for (int row = 0; row < steps_->count(); ++row) {
        auto* item = steps_->item(row);
        const bool done = row < page_id;
        const bool current = row == page_id;
        const QString mark = done ? QStringLiteral("\u2713")
                                  : QStringLiteral("%1").arg(row + 1, 2, 10, QLatin1Char('0'));
        item->setText(QStringLiteral("%1   %2").arg(mark, step_names().at(row)));
        item->setSelected(current);
        auto font = item->font();
        font.setWeight(current ? QFont::Medium : QFont::Normal);
        item->setFont(font);
        item->setForeground(QColor(QString::fromLatin1(
            done ? ui::Theme::Color::kFgDim
                 : (current ? ui::Theme::Color::kFg : ui::Theme::Color::kFgFaint))));
    }
    steps_->setCurrentRow(page_id);

    // "step 4 of 8", where the wizard's own buttons are. A wizard that
    // shows a rail and still cannot say how far along it is has told
    // you where you are without telling you how much is left.
    setButtonText(CustomButton1, QStringLiteral("step %1 of %2")
                                     .arg(page_id + 1)
                                     .arg(steps_->count()));

    // Next and Finish carry the primary weight; Back does not. Set on
    // every page change because QWizard swaps which of them is showing.
    for (const auto role : {NextButton, FinishButton}) {
        if (auto* button = this->button(role)) {
            button->setObjectName(QString::fromLatin1(ui::Theme::Role::kPrimary));
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }
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
