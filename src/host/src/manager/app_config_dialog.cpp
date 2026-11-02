#include "lsw/host/manager/app_config_dialog.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/remote_file_dialog.hpp"
#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/field_stack.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;

constexpr int kIconPx = 64;
constexpr int kEnvBoxPx = 72;
constexpr int kPathIdleMs = 600;
constexpr int kStoreIconPx = 256;

}

AppConfigDialog::AppConfigDialog(apps::AppConfig initial, link::AgentRpcClient& rpc,
                                 QWidget* parent)
    : QDialog(parent),
      cfg_(std::move(initial)),
      rpc_(rpc) {
    setWindowTitle(cfg_.id.isEmpty() ? "New application" : "Configure application");
    setModal(true);
    setMinimumWidth(520);

    const auto chrome = ui::DialogChrome::install(this);
    auto* root = chrome.body;

    auto* identity = new QHBoxLayout;
    identity->setSpacing(14);
    icon_label_ = new QLabel;
    icon_label_->setFixedSize(kIconPx, kIconPx);
    icon_label_->setAlignment(Qt::AlignCenter);
    icon_label_->setObjectName(QStringLiteral("IconSlot"));
    auto* titles = new QVBoxLayout;
    titles->setSpacing(3);
    identity_name_ = new QLabel(cfg_.name.isEmpty() ? QStringLiteral("New application")
                                                    : cfg_.name);
    identity_name_->setObjectName(QString::fromLatin1(Theme::Role::kHeading));
    icon_status_ = Widgets::meta(cfg_.icon_image().isNull()
                                     ? QStringLiteral("no icon yet")
                                     : QStringLiteral("the icon kept from the last read"),
                                 true);
    titles->addWidget(identity_name_);
    titles->addWidget(icon_status_);
    identity->addWidget(icon_label_);
    identity->addLayout(titles, 1);
    auto* replace_icon = Widgets::button(QStringLiteral("Replace icon"), Theme::Role::kQuiet);
    identity->addWidget(replace_icon, 0, Qt::AlignTop);
    root->addLayout(identity);
    connect(replace_icon, &QPushButton::clicked, this, &AppConfigDialog::fetch_icon);

    auto* form = new QVBoxLayout;
    form->setSpacing(12);
    ui::FieldStack fields{form};

    name_edit_ = new QLineEdit(cfg_.name);
    name_edit_->setPlaceholderText("Display name");
    fields.add(QStringLiteral("Name"), name_edit_);

    const bool store = !cfg_.aumid.isEmpty();
    path_edit_ = new QLineEdit(cfg_.target());
    path_edit_->setCursorPosition(0);
    path_edit_->setPlaceholderText("C:\\Program Files\\App\\app.exe");
    auto* path_row = new QWidget;
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->setSpacing(8);
    auto* browse = new QPushButton(QStringLiteral("Browse the guest"));
    path_layout->addWidget(path_edit_, 1);
    path_layout->addWidget(browse);
    if (store) {
        path_edit_->setReadOnly(true);
        browse->hide();
        fields.add(QStringLiteral("Store application"), path_row);
        fields.add_note(QStringLiteral("Windows starts it by this ID; there is no path to change."));
    } else {
        fields.add(QStringLiteral("Path in Windows"), path_row);
        fields.add_note(QStringLiteral("Browsing opens a file picker that reads the guest, "
                                       "not this machine."));
    }

    connect(browse, &QPushButton::clicked, this, [this] {
        RemoteFileDialog picker{rpc_, RemoteFileDialog::Mode::Program, this};
        if (picker.exec() != QDialog::Accepted || picker.selected_path().isEmpty()) {
            return;
        }
        path_edit_->setText(picker.selected_path());
        path_edit_->setCursorPosition(0);
        if (!picker.selected_workdir().isEmpty() && workdir_edit_->text().isEmpty()) {
            workdir_edit_->setText(picker.selected_workdir());
        }
        if (!picker.selected_arguments().isEmpty() && cmdline_edit_->text().isEmpty()) {
            cmdline_edit_->setText(picker.selected_arguments());
        }
        if (name_edit_->text().isEmpty()) {
            const QString file = picker.selected_path().section(QLatin1Char('\\'), -1);
            name_edit_->setText(file.section(QLatin1Char('.'), 0, 0));
        }
    });

    workdir_edit_ = new QLineEdit(cfg_.workdir);
    workdir_edit_->setPlaceholderText(
        QStringLiteral("Defaults to the folder the program is in"));
    auto* workdir_row = new QWidget;
    auto* workdir_layout = new QHBoxLayout(workdir_row);
    workdir_layout->setContentsMargins(0, 0, 0, 0);
    workdir_layout->setSpacing(8);
    auto* browse_dir = new QPushButton(QStringLiteral("Browse"));
    workdir_layout->addWidget(workdir_edit_, 1);
    workdir_layout->addWidget(browse_dir);
    if (store) {
        workdir_row->setParent(this);
        workdir_row->hide();
    } else {
        fields.add(QStringLiteral("Working folder"), workdir_row);
    }
    connect(browse_dir, &QPushButton::clicked, this, [this] {
        RemoteFileDialog picker{rpc_, RemoteFileDialog::Mode::Directory, this};
        if (picker.exec() == QDialog::Accepted && !picker.selected_path().isEmpty()) {
            workdir_edit_->setText(picker.selected_path());
        }
    });

    cmdline_edit_ = new QLineEdit(cfg_.cmdline);
    cmdline_edit_->setPlaceholderText("Optional extra arguments");
    fields.add(QStringLiteral("Arguments"), cmdline_edit_);

    env_edit_ = new QPlainTextEdit(cfg_.env);
    env_edit_->setPlaceholderText("One per line, e.g.\nGALLIUM_DRIVER=llvmpipe");
    env_edit_->setFixedHeight(kEnvBoxPx);
    if (store) {
        env_edit_->setParent(this);
        env_edit_->hide();
    } else {
        fields.add(QStringLiteral("Environment"), env_edit_);
    }

    publish_box_ = new QCheckBox(QStringLiteral("Show in my application menu"));
    publish_box_->setChecked(cfg_.publish_entry);
    form->addWidget(publish_box_);

    auto* found_row = new QWidget;
    auto* found_layout = new QHBoxLayout(found_row);
    found_layout->setContentsMargins(0, 0, 0, 0);
    found_layout->setSpacing(8);
    auto* found = new QLineEdit(cfg_.file_extensions.isEmpty() ? QStringLiteral("-")
                                                                : cfg_.file_extensions.join(QLatin1Char(' ')));
    found->setReadOnly(true);
    found->setCursorPosition(0);
    make_default_ = new QPushButton(QStringLiteral("Make it the default"));
    const bool declared = !cfg_.id.isEmpty() && cfg_.publish_entry && apps::DesktopLauncher::publishing() &&
                          !apps::DesktopLauncher::mime_types_of(cfg_).isEmpty();
    make_default_->setEnabled(declared);
    make_default_->setToolTip(declared ? QStringLiteral("Make a double click on these files start this application")
                                       : QStringLiteral("Needs file types Windows lists for it, and an entry in your menu"));
    found_layout->addWidget(found, 1);
    found_layout->addWidget(make_default_);
    fields.add(QStringLiteral("Opens these file types"), found_row);
    fields.add_note(QStringLiteral("Found in Windows. Types nothing on this machine opens start this "
                                   "application on a double click; Make it the default also does it "
                                   "for the rest."));
    connect(make_default_, &QPushButton::clicked, this, [this] { make_default(); });

    mime_edit_ = new QLineEdit(cfg_.mime_types.split(QLatin1Char('\n'),
                                                     Qt::SkipEmptyParts)
                                   .join(QStringLiteral(", ")));
    mime_edit_->setPlaceholderText(
        QStringLiteral("Optional, added to the ones found: image/vnd.adobe.photoshop"));
    fields.add(QStringLiteral("MIME types"), mime_edit_);

    root->addLayout(form);
    root->addStretch(1);

    auto* footer = chrome.foot;
    auto* remove = Widgets::button(QStringLiteral("Remove"), Theme::Role::kQuiet);
    remove->setVisible(!cfg_.id.isEmpty());
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    auto* save = Widgets::button(QStringLiteral("Save"), Theme::Role::kPrimary);
    save->setDefault(true);
    footer->addWidget(remove);
    footer->addStretch(1);
    footer->addWidget(cancel);
    footer->addWidget(save);

    connect(save, &QPushButton::clicked, this, &AppConfigDialog::on_accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(remove, &QPushButton::clicked, this, [this] {
        removal_requested_ = true;
        accept();
    });
    connect(name_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        identity_name_->setText(text.isEmpty() ? QStringLiteral("New application") : text);
    });
    connect(path_edit_, &QLineEdit::textChanged, this, &AppConfigDialog::on_path_changed);

    path_idle_timer_.setSingleShot(true);
    path_idle_timer_.setInterval(kPathIdleMs);
    connect(&path_idle_timer_, &QTimer::timeout, this, &AppConfigDialog::on_path_idle);

    update_icon_preview();
}

void AppConfigDialog::make_default() {
    const QStringList types = apps::DesktopLauncher::mime_types_of(cfg_);
    if (types.isEmpty()) {
        return;
    }
    QString question = QStringLiteral("Make %1 the application that opens %2 file types on a double click?")
                           .arg(cfg_.name.isEmpty() ? QStringLiteral("this application") : cfg_.name)
                           .arg(types.size());
    if (!cfg_.file_extensions.isEmpty()) {
        question += QStringLiteral("\n\n%1").arg(cfg_.file_extensions.join(QLatin1Char(' ')));
    }
    if (const auto current = apps::DesktopLauncher::default_handler(types.first())) {
        question += QStringLiteral("\n\nFor %1 that is %2 now.")
                        .arg(types.first(), current->name.isEmpty() ? current->entry : current->name);
    }
    if (QMessageBox::question(this, QStringLiteral("Make it the default"), question) != QMessageBox::Yes) {
        return;
    }
    QString why;
    if (!apps::DesktopLauncher::make_default(cfg_, &why)) {
        QMessageBox::warning(this, QStringLiteral("Make it the default"),
                             QStringLiteral("The default was not changed: %1").arg(why));
    }
}

void AppConfigDialog::on_path_changed(const QString& ) {
    path_idle_timer_.start();
}

void AppConfigDialog::on_path_idle() {
    fetch_icon();
}

void AppConfigDialog::fetch_icon() {
    if (!cfg_.aumid.isEmpty()) {
        fetch_store_icon();
        return;
    }
    const QString path = path_edit_->text().trimmed();
    if (path.isEmpty()) {
        icon_status_->setText("(no path)");
        return;
    }
    if (!rpc_.has_target()) {
        icon_status_->setText(QStringLiteral("No guest is set up, so there is no icon to read."));
        return;
    }
    icon_status_->setText(QStringLiteral("Asking the guest for its icon"));
    rpc_.request_async(linkproto::cmd::kGetIcon, {{"path", path.toStdString()}}, this,
                       [this](const link::AgentReply& reply) {
        const auto b64 = reply.ok ? reply.body.value("icon_png_base64", std::string{})
                                  : std::string{};
        if (b64.empty()) {
            icon_status_->setText(
                reply.ok ? QStringLiteral("Windows has no icon for that path.")
                         : QStringLiteral("The agent did not answer: %1")
                               .arg(QString::fromStdString(reply.error)));
            return;
        }
        cfg_.icon_png_base64 = QString::fromStdString(b64);
        update_icon_preview();
        icon_status_->setText(QStringLiteral("Read from the guest just now."));
    });
}

void AppConfigDialog::fetch_store_icon() {
    if (!rpc_.has_target()) {
        icon_status_->setText(QStringLiteral("No guest is set up, so there is no icon to read."));
        return;
    }
    icon_status_->setText(QStringLiteral("Asking the guest for its icon"));
    rpc_.request_async(linkproto::cmd::kListStoreApps,
                       {{"aumid", cfg_.aumid.toStdString()}, {"icon_size", kStoreIconPx}}, this,
                       [this](const link::AgentReply& reply) {
        const auto apps = reply.ok ? reply.body.value("apps", nlohmann::json::array())
                                   : nlohmann::json::array();
        const auto b64 = apps.empty() ? std::string{}
                                      : apps.front().value("icon_png_base64", std::string{});
        if (b64.empty()) {
            icon_status_->setText(
                !reply.ok ? QStringLiteral("The agent did not answer: %1")
                                .arg(QString::fromStdString(reply.error))
                : apps.empty() ? QStringLiteral("Windows no longer has this application.")
                               : QStringLiteral("Windows has no icon for it."));
            return;
        }
        cfg_.icon_png_base64 = QString::fromStdString(b64);
        update_icon_preview();
        icon_status_->setText(QStringLiteral("Read from the guest just now."));
    });
}

void AppConfigDialog::update_icon_preview() {
    QImage img = cfg_.icon_image();
    if (img.isNull()) {
        icon_label_->setPixmap({});
        return;
    }
    icon_label_->setPixmap(
        QPixmap::fromImage(img).scaled(kIconPx, kIconPx, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

void AppConfigDialog::on_accept() {
    cfg_.name = name_edit_->text().trimmed();
    if (cfg_.aumid.isEmpty()) {
        cfg_.path = path_edit_->text().trimmed();
    }
    cfg_.cmdline = cmdline_edit_->text();
    cfg_.workdir = workdir_edit_->text().trimmed();
    cfg_.env = env_edit_->toPlainText();
    cfg_.publish_entry = publish_box_->isChecked();
    QStringList types;
    for (const auto& piece : mime_edit_->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = piece.trimmed();
        if (!trimmed.isEmpty()) {
            types << trimmed;
        }
    }
    cfg_.mime_types = types.join(QLatin1Char('\n'));
    if (cfg_.id.isEmpty() && !cfg_.target().isEmpty()) {
        cfg_.id = apps::AppConfig::make_id(cfg_.target());
    } else if (cfg_.id.isEmpty()) {
        cfg_.id = apps::AppConfig::make_id(cfg_.name);
    }
    if (cfg_.name.isEmpty() || cfg_.target().isEmpty()) {
        return;
    }
    accept();
}

}
