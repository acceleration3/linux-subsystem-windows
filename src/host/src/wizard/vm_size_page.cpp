#include "lsw/host/wizard/vm_size_page.hpp"
#include "lsw/host/wizard/page_chrome.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
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
// Field names in the same uppercase mono every caption in the
// application uses, so a form reads as part of it rather than as a
// dialog somebody else wrote.
QLabel* field_label(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QString::fromLatin1(lsw::host::ui::Theme::Role::kCaption));
    return label;
}

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

    auto* body = PageChrome::install(
        this, QStringLiteral("Size the machine and name the account"),
        QStringLiteral("Both are baked into the answer file, so they are set before the "
                       "installer boots."));

    // The account, two by two. Stacked captions and full-width fields:
    // a name and a password are the same shape, and the pair reads as
    // one question rather than four rows of a form.
    auto* account = new QGridLayout;
    account->setHorizontalSpacing(16);
    account->setVerticalSpacing(12);
    const auto stacked = [](const QString& name, QWidget* control) {
        auto* column = new QVBoxLayout;
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(5);
        column->addWidget(field_label(name));
        column->addWidget(control);
        return column;
    };

    user_edit_ = new QLineEdit;
    user_edit_->setPlaceholderText(QStringLiteral("e.g. lsw"));
    pass_edit_ = new QLineEdit;
    pass_edit_->setPlaceholderText(QStringLiteral("password"));
    pass_edit_->setEchoMode(QLineEdit::Password);
    computer_edit_ = new QLineEdit;
    computer_edit_->setPlaceholderText(QString::fromLatin1(config::kVmName));

    edition_combo_ = new QComboBox;
    edition_combo_->setEnabled(false);
    edition_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    edition_combo_->setMinimumContentsLength(16);
    edition_status_ = new QLabel;
    edition_status_->setObjectName(QString::fromLatin1(lsw::host::ui::Theme::Role::kMeta));
    edition_status_->setWordWrap(true);

    account->addLayout(stacked(QStringLiteral("Username"), user_edit_), 0, 0);
    account->addLayout(stacked(QStringLiteral("Password"), pass_edit_), 0, 1);
    account->addLayout(stacked(QStringLiteral("Computer name"), computer_edit_), 1, 0);
    account->addLayout(stacked(QStringLiteral("Edition"), edition_combo_), 1, 1);
    account->addWidget(edition_status_, 2, 1);
    account->setColumnStretch(0, 1);
    account->setColumnStretch(1, 1);
    body->addLayout(account);

    // A rule between the account and the machine: two questions, and
    // the design draws the seam.
    auto* rule = new QFrame;
    rule->setObjectName(QString::fromLatin1(lsw::host::ui::Theme::Role::kSeparator));
    rule->setFixedHeight(1);
    body->addWidget(rule);

    // The machine. A narrow caption, then as much slider as the page
    // will give, then what it currently says.
    auto* sizing = new QFormLayout;
    sizing->setHorizontalSpacing(16);
    sizing->setVerticalSpacing(12);
    sizing->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    sizing->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    body->addLayout(sizing);
    form_ = sizing;

    sizing->addRow(field_label(QStringLiteral("Cores")),
                   make_slider_row(1, 32, 1, 4, QStringLiteral(" cores"), cpu_spin_));
    sizing->addRow(field_label(QStringLiteral("Memory")),
                   make_slider_row(2048, 65536, 1024, 8192, QStringLiteral(" MiB"),
                                   mem_spin_));
    sizing->addRow(field_label(QStringLiteral("Disk")),
                   make_slider_row(30, 512, 5, 60, QStringLiteral(" GiB"), disk_spin_));

    // What the sizing costs, next to the control that decides it.
    footnote_ = new QLabel;
    footnote_->setObjectName(QString::fromLatin1(lsw::host::ui::Theme::Role::kMeta));
    footnote_->setWordWrap(true);
    body->addWidget(footnote_);
    body->addStretch(1);

    connect(disk_spin_, &QSpinBox::valueChanged, this,
            [this](int) { refresh_footnote(); });
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
    refresh_footnote();
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
    // The field stays, whatever the answer is. It is one of the four
    // questions this page asks, and an empty place where it was reads
    // as something gone wrong rather than as a question that cannot be
    // answered yet.
    const QString iso = s.chosen_iso_path;
    if (iso.isEmpty()) {
        edition_combo_->clear();
        edition_combo_->setPlaceholderText(QStringLiteral("Read from the image"));
        edition_combo_->setEnabled(false);
        edition_status_->setText(
            QStringLiteral("Pick an image first: the editions are read out of it."));
        editions_loaded_ = true;
        Q_EMIT completeChanged();
        return;
    }

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

// How much of the host the sizing leaves behind. A number next to the
// slider that moves it is worth more than the same number in a manual.
void VmSizePage::refresh_footnote() {
    const qint64 free_gib = wizard_.state().host_free_disk_bytes / (1024LL * 1024 * 1024);
    footnote_->setText(QStringLiteral("Leaves %1 GiB free on the host. The disk is sparse "
                                      "and grows as the guest fills it.")
                           .arg(qMax<qint64>(0, free_gib - disk_spin_->value())));
}

void VmSizePage::on_editions_ready() {
    const auto editions = edition_watcher_->result();
    edition_combo_->clear();
    if (editions.isEmpty()) {
        edition_status_->setText("Could not read the editions from this image. Setup will "
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

    // This is the last page that gathers anything, so the pipeline
    // starts here. It used to start on the trim page, which no longer
    // exists: trimming is a tool, run before this point or not at all.
    wizard_.start_pipeline();
    return true;
}

int VmSizePage::nextId() const {
    return Wizard::Payload;
}

}  // namespace lsw::host::wizard
