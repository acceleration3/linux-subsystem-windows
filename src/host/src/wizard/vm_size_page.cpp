#include "lsw/host/wizard/vm_size_page.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace lsw::host::wizard {

namespace {

// Pair a slider and a spin box so the spin box is the source of truth
// (it owns the value), the slider mirrors it, and changing either
// updates the other. Returns the row widget for embedding in a layout;
// the caller keeps the spin box for reading the value back.
QWidget* make_slider_row(int min, int max, int step, int initial, const QString& suffix,
                         QSpinBox*& spin_out) {
    auto* row = new QWidget;
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setPageStep(step * 4);
    slider->setValue(initial);

    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setSuffix(suffix);
    spin->setValue(initial);
    spin->setMinimumWidth(120);

    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    lay->addWidget(slider, 1);
    lay->addWidget(spin);
    spin_out = spin;
    return row;
}

}  // namespace

VmSizePage::VmSizePage(Wizard& wizard, QWidget* parent) : QWizardPage(parent), wizard_(wizard) {
    setTitle("VM size and account");
    setSubTitle(QStringLiteral("Size the \"%1\" VM, pick the local Windows "
                                 "account, and choose the Windows edition.")
                    .arg(QString::fromLatin1(config::kVmName)));

    auto* form = new QFormLayout(this);
    // Let the field column eat all horizontal slack so the (sometimes
    // 30+ char) edition names aren't clipped into a tiny combo.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Local Windows account credentials. Baked into autounattend.xml's
    // oobeSystem pass — must be set before the install ISO is built.
    user_edit_ = new QLineEdit;
    user_edit_->setPlaceholderText("e.g. lsw");
    pass_edit_ = new QLineEdit;
    pass_edit_->setPlaceholderText("password");
    pass_edit_->setEchoMode(QLineEdit::Password);
    computer_edit_ = new QLineEdit;
    computer_edit_->setPlaceholderText(QString::fromLatin1(config::kVmName));
    form->addRow("Username", user_edit_);
    form->addRow("Password", pass_edit_);
    form->addRow("Computer name", computer_edit_);

    // Edition combo — populated async from the ISO's WIM image table.
    edition_combo_ = new QComboBox;
    edition_combo_->setEnabled(false);
    edition_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Don't let a long current item dictate a huge size hint; the
    // expanding policy + form growth handle width, and the popup shows
    // full names regardless.
    edition_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    edition_combo_->setMinimumContentsLength(20);
    edition_status_ = new QLabel;
    edition_status_->setStyleSheet("color: gray;");
    edition_status_->setWordWrap(true);
    form->addRow("Edition", edition_combo_);
    form->addRow(QString{}, edition_status_);

    // Defaults plugged in by initializePage() once we've inspected the host.
    form->addRow("CPU cores", make_slider_row(1, 32, 1, 4, " cores", cpu_spin_));
    form->addRow("Memory", make_slider_row(2048, 65536, 1024, 8192, " MiB", mem_spin_));
    form->addRow("Disk", make_slider_row(30, 512, 5, 60, " GiB", disk_spin_));

    connect(user_edit_, &QLineEdit::textChanged, this, &VmSizePage::completeChanged);
    connect(pass_edit_, &QLineEdit::textChanged, this, &VmSizePage::completeChanged);

    edition_watcher_ = new QFutureWatcher<QVector<provision::install::WimEdition>>(this);
    connect(edition_watcher_, &QFutureWatcher<QVector<provision::install::WimEdition>>::finished, this,
            &VmSizePage::on_editions_ready);
}

void VmSizePage::initializePage() {
    auto& s = wizard_.state();
    cpu_spin_->setMaximum(std::max(2, s.host_cpu_threads));
    mem_spin_->setMaximum(std::max(4096, s.host_ram_mib));
    if (s.host_free_disk_bytes > 0) {
        const int free_gib = static_cast<int>(s.host_free_disk_bytes / (1024LL * 1024LL * 1024LL));
        disk_spin_->setMaximum(std::max(60, free_gib - 10));
    }
    if (user_edit_->text().isEmpty()) {
        user_edit_->setText(s.user_name);
    }
    if (pass_edit_->text().isEmpty()) {
        pass_edit_->setText(s.user_password);
    }
    if (computer_edit_->text().isEmpty()) {
        computer_edit_->setText(s.computer_name);
    }

    // Edition list only makes sense for a local ISO — when downloading
    // from Microsoft the ISO doesn't exist yet, so we can't read its
    // WIM. In that case hide the row and keep image_index at 1.
    const QString iso = s.chosen_iso_path;
    if (iso.isEmpty()) {
        edition_combo_->setVisible(false);
        edition_status_->setVisible(false);
        editions_loaded_ = true;
        Q_EMIT completeChanged();
        return;
    }
    edition_combo_->setVisible(true);
    edition_status_->setVisible(true);

    if (probed_iso_ == iso) {
        return;  // already listed this ISO; keep the existing combo
    }
    probed_iso_ = iso;
    editions_loaded_ = false;
    edition_combo_->clear();
    edition_combo_->setEnabled(false);
    edition_status_->setText("Reading editions from the ISO (this can take ~10 s)...");
    Q_EMIT completeChanged();
    const QString scratch = wizard_.services().settings().cache_dir();
    edition_watcher_->setFuture(QtConcurrent::run(
        [iso, scratch] { return provision::install::WimEditionLister::list(iso, scratch, nullptr); }));
}

void VmSizePage::on_editions_ready() {
    const auto editions = edition_watcher_->result();
    edition_combo_->clear();
    if (editions.isEmpty()) {
        edition_status_->setText("Could not read editions from the ISO — Setup will "
                                 "install image 1 (or prompt if that fails).");
        editions_loaded_ = true;  // don't block the wizard; fall back to index 1
        Q_EMIT completeChanged();
        return;
    }
    for (const auto& e : editions) {
        edition_combo_->addItem(e.name, e.index);
    }
    // Default to the first edition that looks like "Pro" if present —
    // it's the most useful for an LSW app-host VM — else image 1.
    int def = 0;
    for (int i = 0; i < editions.size(); ++i) {
        if (editions[i].name.contains(QStringLiteral("Pro"), Qt::CaseInsensitive)) {
            def = i;
            break;
        }
    }
    edition_combo_->setCurrentIndex(def);
    edition_combo_->setEnabled(true);
    edition_status_->setText(QStringLiteral("%1 editions found.").arg(editions.size()));
    editions_loaded_ = true;
    Q_EMIT completeChanged();
}

bool VmSizePage::isComplete() const {
    // Block Next until the edition probe has settled so the pipeline
    // doesn't start with an unknown image index.
    return editions_loaded_;
}

bool VmSizePage::validatePage() {
    auto& s = wizard_.state();
    s.draft.sizing.cpu_cores = cpu_spin_->value();
    s.draft.sizing.memory_mib = mem_spin_->value();
    s.draft.sizing.disk_gib = disk_spin_->value();
    s.user_name = user_edit_->text().trimmed();
    s.user_password = pass_edit_->text();
    if (s.user_name.isEmpty()) {
        s.user_name = "lsw";
    }
    if (s.user_password.isEmpty()) {
        s.user_password = "lsw";
    }
    // Computer name — empty keeps the pipeline's VM-name fallback.
    s.computer_name = computer_edit_->text().trimmed();
    // Edition → WIM image index. currentData() carries the 1-based
    // index; default to 1 when the combo is empty/hidden (download
    // path or unreadable ISO).
    const QVariant idx = edition_combo_->currentData();
    s.image_index = idx.isValid() ? idx.toInt() : 1;

    // All inputs collected — fire the install pipeline. Subsequent
    // pages (Download/Install/Setup) are presentation only.
    wizard_.start_pipeline();
    return true;
}

int VmSizePage::nextId() const {
    // Local ISO → straight to the Payload (build) page; the pipeline
    // skips DownloadingIso. Microsoft-download → DownloadPage first,
    // which then advances to Payload.
    return wizard_.state().chosen_iso_path.isEmpty() ? Wizard::Download : Wizard::Payload;
}

}  // namespace lsw::host::wizard
