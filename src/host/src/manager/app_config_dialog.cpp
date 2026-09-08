#include "lsw/host/manager/app_config_dialog.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/remote_file_dialog.hpp"

#include <QByteArray>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFutureWatcher>
#include <QPixmap>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

constexpr int kIconPx = 64;
constexpr int kEnvBoxPx = 72;  // ~3 lines; enough to see what is set
constexpr int kPathIdleMs = 600;

// Small RPC helper — synchronous get_icon over the shared channel.
QString agent_rpc_get_icon(link::AgentRpcClient& rpc, const QString& path) {
    nlohmann::json req = {
        {"cmd", linkproto::cmd::kGetIcon},
        {"params", {{"path", path.toStdString()}}},
    };
    const auto reply = rpc.call(req);
    if (reply.isEmpty()) {
        return {};
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                              reply.constData() + reply.size());
        return QString::fromStdString(j.value("icon_png_base64", std::string{}));
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

}  // namespace

AppConfigDialog::AppConfigDialog(apps::AppConfig initial, link::AgentRpcClient& rpc,
                                 QWidget* parent)
    : QDialog(parent),
      cfg_(std::move(initial)),
      rpc_(rpc) {
    setWindowTitle(cfg_.id.isEmpty() ? "New application" : "Configure application");
    setModal(true);
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    name_edit_ = new QLineEdit(cfg_.name);
    name_edit_->setPlaceholderText("Display name");
    form->addRow("Name", name_edit_);

    // The path is a Windows path, so browsing for it has to happen
    // inside Windows. The button opens a picker that reads the guest
    // over the agent connection, not this machine's disk.
    path_edit_ = new QLineEdit(cfg_.path);
    path_edit_->setPlaceholderText("C:\\Program Files\\App\\app.exe");
    auto* path_row = new QWidget;
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->setSpacing(8);
    auto* browse = new QPushButton(QStringLiteral("Browse the guest"));
    path_layout->addWidget(path_edit_, 1);
    path_layout->addWidget(browse);
    form->addRow("Path in Windows", path_row);

    connect(browse, &QPushButton::clicked, this, [this] {
        RemoteFileDialog picker{rpc_, RemoteFileDialog::Mode::Program, this};
        if (picker.exec() != QDialog::Accepted || picker.selected_path().isEmpty()) {
            return;
        }
        path_edit_->setText(picker.selected_path());
        // A shortcut carries its own arguments and working folder, and
        // dropping them is how an application ends up launching without
        // the switches its Start menu entry always passed it.
        if (!picker.selected_arguments().isEmpty() && cmdline_edit_->text().isEmpty()) {
            cmdline_edit_->setText(picker.selected_arguments());
        }
        if (name_edit_->text().isEmpty()) {
            const QString file = picker.selected_path().section(QLatin1Char('\\'), -1);
            name_edit_->setText(file.section(QLatin1Char('.'), 0, 0));
        }
    });

    cmdline_edit_ = new QLineEdit(cfg_.cmdline);
    cmdline_edit_->setPlaceholderText("Optional extra arguments");
    form->addRow("Arguments", cmdline_edit_);

    // Arguments and environment are not interchangeable, and the
    // difference catches people out: a variable typed into Arguments
    // reaches the application as a filename to open. Hence a field of
    // its own, and a placeholder that shows the shape.
    env_edit_ = new QPlainTextEdit(cfg_.env);
    env_edit_->setPlaceholderText("One per line, e.g.\nGALLIUM_DRIVER=llvmpipe");
    env_edit_->setFixedHeight(kEnvBoxPx);
    form->addRow("Environment", env_edit_);

    // Where it appears on this desktop. The switch in Settings decides
    // whether any application is published; this decides whether this
    // one is, because a guest with forty Start menu shortcuts should
    // not put forty entries in somebody's launcher.
    publish_box_ = new QCheckBox(QStringLiteral("Show in my application menu"));
    publish_box_->setChecked(cfg_.publish_entry);
    form->addRow(QString{}, publish_box_);

    mime_edit_ = new QLineEdit(cfg_.mime_types.split(QLatin1Char('\n'),
                                                     Qt::SkipEmptyParts)
                                   .join(QStringLiteral(", ")));
    mime_edit_->setPlaceholderText(
        QStringLiteral("image/vnd.adobe.photoshop, application/x-ms-dos-executable"));
    form->addRow(QStringLiteral("Opens file types"), mime_edit_);

    // -1 is "any", which is what every application did before there was
    // a choice, and is still the right default: pinning matters only
    // when you want one application's counters or its ring size kept
    // apart from everything else.
    channel_combo_ = new QComboBox;
    channel_combo_->addItem(QStringLiteral("Any free channel"), -1);
    for (int i = 0; i < 8; ++i) {
        channel_combo_->addItem(QStringLiteral("Channel %1").arg(i), i);
    }
    if (const int index = channel_combo_->findData(cfg_.channel); index >= 0) {
        channel_combo_->setCurrentIndex(index);
    }
    form->addRow(QStringLiteral("Render channel"), channel_combo_);

    icon_label_ = new QLabel;
    icon_label_->setFixedSize(kIconPx, kIconPx);
    icon_label_->setStyleSheet("background-color: #222; border: 1px solid #444;");
    icon_label_->setAlignment(Qt::AlignCenter);
    icon_status_ = new QLabel("(no icon)");
    icon_status_->setStyleSheet("color: gray;");
    auto* icon_row = new QHBoxLayout;
    icon_row->addWidget(icon_label_);
    icon_row->addWidget(icon_status_, 1);
    auto* icon_widget = new QWidget;
    icon_widget->setLayout(icon_row);
    form->addRow("Icon", icon_widget);

    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &AppConfigDialog::on_accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(path_edit_, &QLineEdit::textChanged, this, &AppConfigDialog::on_path_changed);

    path_idle_timer_.setSingleShot(true);
    path_idle_timer_.setInterval(kPathIdleMs);
    connect(&path_idle_timer_, &QTimer::timeout, this, &AppConfigDialog::on_path_idle);

    update_icon_preview();
}

void AppConfigDialog::on_path_changed(const QString& /*text*/) {
    path_idle_timer_.start();  // restart on every keystroke
}

void AppConfigDialog::on_path_idle() {
    fetch_icon();
}

void AppConfigDialog::fetch_icon() {
    const QString path = path_edit_->text().trimmed();
    if (path.isEmpty()) {
        icon_status_->setText("(no path)");
        return;
    }
    if (!rpc_.has_target()) {
        icon_status_->setText("(VM not running — can't fetch icon)");
        return;
    }
    icon_status_->setText("Fetching from guest agent...");
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString b64 = watcher->result();
        watcher->deleteLater();
        if (b64.isEmpty()) {
            icon_status_->setText("Could not fetch icon for that path.");
            return;
        }
        cfg_.icon_png_base64 = b64;
        update_icon_preview();
        icon_status_->setText("Icon fetched.");
    });
    watcher->setFuture(QtConcurrent::run([this, path] {
        return agent_rpc_get_icon(rpc_, path);
    }));
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
    cfg_.path = path_edit_->text().trimmed();
    cfg_.cmdline = cmdline_edit_->text();
    cfg_.env = env_edit_->toPlainText();
    cfg_.publish_entry = publish_box_->isChecked();
    cfg_.channel = channel_combo_->currentData().toInt();
    // Typed with commas because that is how people write a list; stored
    // one per line because that is how the .desktop file wants them.
    QStringList types;
    for (const auto& piece : mime_edit_->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = piece.trimmed();
        if (!trimmed.isEmpty()) {
            types << trimmed;
        }
    }
    cfg_.mime_types = types.join(QLatin1Char('\n'));
    if (cfg_.id.isEmpty() && !cfg_.path.isEmpty()) {
        cfg_.id = apps::AppConfig::make_id(cfg_.path);
    } else if (cfg_.id.isEmpty()) {
        cfg_.id = apps::AppConfig::make_id(cfg_.name);
    }
    if (cfg_.name.isEmpty() || cfg_.path.isEmpty()) {
        // Nothing useful to save.
        return;
    }
    accept();
}

}  // namespace lsw::host::manager
