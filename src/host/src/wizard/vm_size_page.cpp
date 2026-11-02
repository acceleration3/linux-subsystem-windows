#include "lsw/host/wizard/vm_size_page.hpp"

#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/provision/install/media_language.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/ui/field_stack.hpp"
#include "lsw/host/ui/machine_limits.hpp"
#include "lsw/host/ui/slider_spin.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace lsw::host::wizard {

namespace {

using ui::Widgets;

constexpr int kEditionIdRole = Qt::UserRole + 1;

ui::SliderSpin* make_slider(const ui::MachineLimits::Range& range, int initial,
                            const QString& suffix) {
    auto* slider = new ui::SliderSpin(range.min, range.max, range.step, suffix);
    slider->spin()->setMinimumWidth(120);
    slider->set_value(initial);
    return slider;
}

}

VmSizePage::VmSizePage(Wizard& wizard, QWidget* parent) : WizardPage(parent), wizard_(wizard) {

    auto* body = install_chrome(
        QStringLiteral("Size the machine and name the account"),
        QStringLiteral("Both are baked into the answer file, so they are set before the "
                       "installer boots."));

    auto* account = new QGridLayout;
    account->setHorizontalSpacing(16);
    account->setVerticalSpacing(12);

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
    edition_status_ = Widgets::meta(QString{}, true);
    language_combo_ = new QComboBox;
    language_combo_->setEnabled(false);
    language_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    language_combo_->setMinimumContentsLength(16);
    language_status_ = Widgets::meta(QString{}, true);

    using ui::FieldStack;
    account->addLayout(FieldStack::column(QStringLiteral("Username"), user_edit_), 0, 0);
    account->addLayout(FieldStack::column(QStringLiteral("Password"), pass_edit_), 0, 1);
    account->addLayout(FieldStack::column(QStringLiteral("Computer name"), computer_edit_), 1, 0);
    account->addLayout(FieldStack::column(QStringLiteral("Edition"), edition_combo_), 1, 1);
    account->addWidget(edition_status_, 2, 1);
    account->addLayout(FieldStack::column(QStringLiteral("Language"), language_combo_), 3, 0);
    account->addWidget(language_status_, 4, 0);
    account->setColumnStretch(0, 1);
    account->setColumnStretch(1, 1);
    body->addLayout(account);

    body->addWidget(Widgets::separator());

    auto* sizing = new QFormLayout;
    sizing->setHorizontalSpacing(16);
    sizing->setVerticalSpacing(12);
    sizing->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    sizing->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    body->addLayout(sizing);
    form_ = sizing;

    const config::VmSizing defaults;
    cpu_slider_ = make_slider(ui::MachineLimits::kCores, defaults.cpu_cores,
                              QStringLiteral(" cores"));
    mem_slider_ = make_slider(ui::MachineLimits::kMemoryMib, defaults.memory_mib,
                              QStringLiteral(" MiB"));
    disk_slider_ = make_slider(ui::MachineLimits::kDiskGib, defaults.disk_gib,
                               QStringLiteral(" GiB"));
    sizing->addRow(Widgets::caption(QStringLiteral("Cores")), cpu_slider_);
    sizing->addRow(Widgets::caption(QStringLiteral("Memory")), mem_slider_);
    sizing->addRow(Widgets::caption(QStringLiteral("Disk")), disk_slider_);

    footnote_ = Widgets::meta(QString{}, true);
    body->addWidget(footnote_);

    if (wizard_.services().vms().primary() != nullptr) {
        auto* replaces = new QLabel(QStringLiteral(
            "Next starts the install, which replaces the current guest. It is shut down, its "
            "disk is deleted, and every application installed in Windows goes with it."));
        replaces->setObjectName(QString::fromLatin1(ui::Theme::Role::kCalloutCritical));
        replaces->setWordWrap(true);
        body->addWidget(replaces);
    }
    body->addStretch(1);

    connect(disk_slider_, &ui::SliderSpin::value_changed, this,
            [this](int) { refresh_footnote(); });
    connect(user_edit_, &QLineEdit::textChanged, this, &VmSizePage::completeChanged);
    connect(pass_edit_, &QLineEdit::textChanged, this, &VmSizePage::completeChanged);

    edition_watcher_ = new QFutureWatcher<QVector<provision::install::WimEdition>>(this);
    connect(edition_watcher_, &QFutureWatcher<QVector<provision::install::WimEdition>>::finished, this,
            &VmSizePage::on_editions_ready);
    language_watcher_ = new QFutureWatcher<QStringList>(this);
    connect(language_watcher_, &QFutureWatcher<QStringList>::finished, this,
            &VmSizePage::on_languages_ready);
}

void VmSizePage::initializePage() {
    auto& s = wizard_.state();
    cpu_slider_->set_maximum(std::max(2, s.host_cpu_threads));
    mem_slider_->set_maximum(std::max(4096, s.host_ram_mib));
    if (s.host_free_disk_bytes > 0) {
        const int free_gib = static_cast<int>(s.host_free_disk_bytes / (1024LL * 1024LL * 1024LL));
        disk_slider_->set_maximum(std::max(60, free_gib - 10));
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

    const QString iso = s.chosen_iso_path;
    if (iso.isEmpty()) {
        edition_combo_->clear();
        edition_combo_->setPlaceholderText(QStringLiteral("Read from the image"));
        edition_combo_->setEnabled(false);
        edition_status_->setText(
            QStringLiteral("Pick an image first: the editions are read out of it."));
        language_combo_->clear();
        language_combo_->setPlaceholderText(QStringLiteral("Read from the image"));
        language_combo_->setEnabled(false);
        language_status_->clear();
        editions_loaded_ = true;
        languages_loaded_ = true;
        Q_EMIT completeChanged();
        return;
    }

    if (probed_iso_ == iso) {
        return;
    }
    probed_iso_ = iso;
    editions_loaded_ = false;
    languages_loaded_ = false;
    edition_combo_->clear();
    edition_combo_->setEnabled(false);
    edition_status_->setText("Reading editions from the ISO (this can take ~10 s)...");
    language_combo_->clear();
    language_combo_->setEnabled(false);
    language_status_->setText(QStringLiteral("Reading the languages from the ISO..."));
    Q_EMIT completeChanged();
    const QString scratch = wizard_.services().settings().cache_dir();
    edition_watcher_->setFuture(QtConcurrent::run(
        [iso, scratch] { return provision::install::WimEditionLister::list(iso, scratch, nullptr); }));
    language_watcher_->setFuture(QtConcurrent::run([iso, scratch] {
        return provision::install::MediaLanguage::of(iso, scratch, nullptr);
    }));
}

namespace {
QString language_label(const QString& tag) {
    const QLocale locale{tag};
    if (locale.language() == QLocale::C) {
        return tag;
    }
    const QString territory = locale.territory() == QLocale::AnyTerritory
                                  ? QString{}
                                  : QStringLiteral(" (%1)").arg(QLocale::territoryToString(locale.territory()));
    return QStringLiteral("%1%2 \u00b7 %3")
        .arg(QLocale::languageToString(locale.language()), territory, tag);
}

}

void VmSizePage::on_languages_ready() {
    const QStringList languages = language_watcher_->result();
    language_combo_->clear();
    for (const QString& tag : languages) {
        language_combo_->addItem(language_label(tag), tag);
    }
    language_combo_->setEnabled(languages.size() > 1);
    language_status_->setText(
        languages.isEmpty()
            ? QStringLiteral("Could not read the languages from this image. Windows is "
                             "installed in its default one.")
        : languages.size() == 1 ? QStringLiteral("The only language this image carries.")
                                : QStringLiteral("%1 languages; the first is the image's "
                                                 "default.").arg(languages.size()));
    languages_loaded_ = true;
    Q_EMIT completeChanged();
}

void VmSizePage::refresh_footnote() {
    const qint64 free_gib = wizard_.state().host_free_disk_bytes / (1024LL * 1024 * 1024);
    footnote_->setText(QStringLiteral("Leaves %1 GiB free on the host. The disk is sparse "
                                      "and grows as the guest fills it.")
                           .arg(qMax<qint64>(0, free_gib - disk_slider_->value())));
}

void VmSizePage::on_editions_ready() {
    const auto editions = edition_watcher_->result();
    edition_combo_->clear();
    if (editions.isEmpty()) {
        edition_status_->setText("Could not read the editions from this image. Setup will "
                                 "install image 1 (or prompt if that fails).");
        editions_loaded_ = true;
        Q_EMIT completeChanged();
        return;
    }
    for (const auto& e : editions) {
        edition_combo_->addItem(e.name, e.index);
        edition_combo_->setItemData(edition_combo_->count() - 1, e.edition_id, kEditionIdRole);
    }
    edition_combo_->setCurrentIndex(provision::install::WimEditionLister::preferred(editions));
    edition_combo_->setEnabled(true);
    edition_status_->setText(QStringLiteral("%1 editions found.").arg(editions.size()));
    editions_loaded_ = true;
    Q_EMIT completeChanged();
}

bool VmSizePage::isComplete() const {
    return editions_loaded_ && languages_loaded_;
}

bool VmSizePage::validatePage() {
    auto& s = wizard_.state();
    s.draft.sizing.cpu_cores = cpu_slider_->value();
    s.draft.sizing.memory_mib = mem_slider_->value();
    s.draft.sizing.disk_gib = disk_slider_->value();
    s.user_name = user_edit_->text().trimmed();
    s.user_password = pass_edit_->text();
    if (s.user_name.isEmpty()) {
        s.user_name = "lsw";
    }
    if (s.user_password.isEmpty()) {
        s.user_password = "lsw";
    }
    s.computer_name = computer_edit_->text().trimmed();
    const QVariant idx = edition_combo_->currentData();
    s.image_index = idx.isValid() ? idx.toInt() : 1;
    s.edition_id = edition_combo_->currentData(kEditionIdRole).toString();
    s.language = language_combo_->currentData().toString();

    wizard_.start_pipeline();
    return true;
}

int VmSizePage::nextId() const {
    return Wizard::Payload;
}

}
