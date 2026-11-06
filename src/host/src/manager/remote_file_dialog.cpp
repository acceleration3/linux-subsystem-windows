#include "lsw/host/manager/remote_file_dialog.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/table_header.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QButtonGroup>
#include <QComboBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <optional>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;

constexpr int kRoleName = Qt::UserRole;
constexpr int kRoleIsDir = Qt::UserRole + 1;

class EntryItem : public QTreeWidgetItem {
public:
    EntryItem(QTreeWidget* parent, bool directory, std::optional<double> size, std::optional<double> modified)
        : QTreeWidgetItem(parent), directory_(directory), size_(size), modified_(modified),
          natural_(parent->topLevelItemCount()) {
    }

    bool operator<(const QTreeWidgetItem& other) const override {
        const auto* rhs = dynamic_cast<const EntryItem*>(&other);
        const QHeaderView* head = treeWidget() != nullptr ? treeWidget()->header() : nullptr;
        if (rhs == nullptr || head == nullptr) {
            return QTreeWidgetItem::operator<(other);
        }
        if (ui::TableHeader::natural(head)) {
            return natural_ < rhs->natural_;
        }
        const bool descending = ui::TableHeader::order(head) == Qt::DescendingOrder;
        if (directory_ != rhs->directory_) {
            return descending ? !directory_ : directory_;
        }
        const int column = treeWidget()->sortColumn();
        if (column > 0) {
            const auto& mine = column == 1 ? size_ : modified_;
            const auto& theirs = column == 1 ? rhs->size_ : rhs->modified_;
            if (mine.has_value() != theirs.has_value()) {
                return descending ? !mine.has_value() : mine.has_value();
            }
            if (mine && theirs && *mine != *theirs) {
                return *mine < *theirs;
            }
        } else {
            const int compared = text(0).compare(rhs->text(0), Qt::CaseInsensitive);
            if (compared != 0) {
                return compared < 0;
            }
        }
        return descending ? natural_ > rhs->natural_ : natural_ < rhs->natural_;
    }

private:
    bool directory_;
    std::optional<double> size_;
    std::optional<double> modified_;
    int natural_;
};

struct Place {
    const char* label;
    const char* path;
    const char* glyph;
};

const Place kPlaces[] = {
    {"Start menu", R"(C:\ProgramData\Microsoft\Windows\Start Menu\Programs)", "▣"},
    {"Program Files", R"(C:\Program Files)", "▢"},
    {"Program Files (x86)", R"(C:\Program Files (x86))", "▢"},
    {"Desktop", R"(C:\Users\Public\Desktop)", "●"},
    {"Downloads", R"(C:\Users\Public\Downloads)", "●"},
};

}

RemoteFileDialog::RemoteFileDialog(link::AgentRpcClient& rpc, Mode mode, QWidget* parent)
    : QDialog(parent), rpc_(rpc), mode_(mode) {
    setWindowTitle(mode_ == Mode::Directory
                       ? QStringLiteral("Choose a folder in Windows")
                       : QStringLiteral("Choose a program in Windows"));
    setModal(true);
    resize(780, 560);
    build();

    if (!rpc_.has_target()) {
        status_->setText(QStringLiteral("No guest is set up, so there is nothing to browse."));
        entries_->setEnabled(false);
        places_->setEnabled(false);
        return;
    }
    navigate_to(QString::fromLatin1(kPlaces[0].path));
}

void RemoteFileDialog::build() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    places_ = new QWidget;
    Theme::surface(places_, Theme::Role::kNavRail);
    places_->setFixedWidth(190);
    places_layout_ = new QVBoxLayout(places_);
    places_layout_->setContentsMargins(8, 9, 8, 9);
    places_layout_->setSpacing(1);
    add_place_heading(QStringLiteral("Shortcuts"));
    for (const auto& place : kPlaces) {
        add_place(QString::fromLatin1(place.label), QString::fromLatin1(place.path),
                  QString::fromUtf8(place.glyph));
    }
    places_layout_->addStretch(1);
    root->addWidget(places_);

    auto* pane = new QWidget;
    auto* pane_layout = new QVBoxLayout(pane);
    pane_layout->setContentsMargins(0, 0, 0, 0);
    pane_layout->setSpacing(0);

    auto* bar = new QWidget;
    Theme::surface(bar, Theme::Role::kContentBar);
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(10, 8, 10, 8);
    bar_layout->setSpacing(6);

    auto* up = Widgets::button(QStringLiteral("↑"), Theme::Role::kQuiet);
    up->setFixedWidth(32);
    bar_layout->addWidget(up);

    crumbs_ = new QHBoxLayout;
    crumbs_->setSpacing(2);
    bar_layout->addLayout(crumbs_);
    bar_layout->addStretch(1);

    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("Search this folder"));
    search_->setFixedWidth(170);
    bar_layout->addWidget(search_);
    pane_layout->addWidget(bar);

    connect(up, &QPushButton::clicked, this,
            [this] { navigate_to(parent_of(current_)); });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < entries_->topLevelItemCount(); ++i) {
            auto* item = entries_->topLevelItem(i);
            item->setHidden(!text.isEmpty()
                            && !item->text(0).contains(text, Qt::CaseInsensitive));
        }
    });

    entries_ = new QTreeWidget;
    entries_->setColumnCount(3);
    entries_->setHeaderLabels({QStringLiteral("NAME"), QStringLiteral("SIZE"),
                               QStringLiteral("MODIFIED")});
    entries_->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    entries_->setRootIsDecorated(false);
    entries_->setUniformRowHeights(true);
    entries_->setAlternatingRowColors(false);
    ui::TableHeader::attach(entries_->header(), QStringLiteral("remote-files"), {420, 96, 140});
    pane_layout->addWidget(entries_, 1);

    connect(entries_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) {
                if (item->data(0, kRoleIsDir).toBool()) {
                    navigate_to(item->data(0, kRoleName).toString());
                } else {
                    name_edit_->setText(item->text(0));
                    accept_current();
                }
            });
    connect(entries_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (item != nullptr && !item->data(0, kRoleIsDir).toBool()) {
                    name_edit_->setText(item->text(0));
                }
            });

    auto* name_row = new QFrame;
    Theme::surface(name_row, Theme::Role::kContentBar);
    auto* name_layout = new QHBoxLayout(name_row);
    name_layout->setContentsMargins(12, 10, 12, 10);
    name_layout->setSpacing(8);
    name_edit_ = new QLineEdit;
    name_edit_->setPlaceholderText(QStringLiteral("File name"));
    filter_ = new QComboBox;
    if (mode_ == Mode::Program) {
        filter_->addItem(QStringLiteral("Programs and shortcuts"), QString{});
        filter_->addItem(QStringLiteral("Programs (.exe)"), QStringLiteral(".exe"));
        filter_->addItem(QStringLiteral("Shortcuts (.lnk)"), QStringLiteral(".lnk"));
        filter_->addItem(QStringLiteral("All files"), QStringLiteral("*"));
    } else {
        filter_->addItem(QStringLiteral("All files"), QStringLiteral("*"));
    }
    filter_->setFixedWidth(200);
    name_layout->addWidget(name_edit_, 1);
    name_layout->addWidget(filter_);
    pane_layout->addWidget(name_row);
    connect(filter_, &QComboBox::currentIndexChanged, this,
            [this](int) { navigate_to(current_); });

    auto* footer_layout = ui::DialogChrome::foot_band(pane_layout, QMargins(12, 10, 12, 10), 8);
    status_ = Widgets::meta(QString{}, true);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    choose_button_ = Widgets::button(QStringLiteral("Choose"), Theme::Role::kPrimary);
    footer_layout->addWidget(status_, 1);
    footer_layout->addWidget(cancel);
    footer_layout->addWidget(choose_button_);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(choose_button_, &QPushButton::clicked, this,
            &RemoteFileDialog::accept_current);

    root->addWidget(pane, 1);
}

void RemoteFileDialog::append_place_widget(QWidget* widget) {
    int at = places_layout_->count();
    if (at > 0 && places_layout_->itemAt(at - 1)->spacerItem() != nullptr) {
        --at;
    }
    places_layout_->insertWidget(at, widget);
}

void RemoteFileDialog::add_place(const QString& label, const QString& path,
                                 const QString& glyph) {
    auto* button = new QPushButton;
    button->setObjectName(QStringLiteral("Place"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("placePath", path);

    auto* layout = new QHBoxLayout(button);
    layout->setContentsMargins(9, 6, 9, 6);
    layout->setSpacing(9);
    auto* mark = new QLabel(glyph);
    mark->setObjectName(QStringLiteral("PlaceGlyph"));
    mark->setFixedWidth(14);
    mark->setAlignment(Qt::AlignCenter);
    auto* text = new QLabel(label);
    text->setObjectName(QStringLiteral("PlaceLabel"));
    layout->addWidget(mark);
    layout->addWidget(text, 1);

    append_place_widget(button);
    place_buttons_.push_back(button);
    connect(button, &QPushButton::clicked, this, [this, path] { navigate_to(path); });
}

void RemoteFileDialog::highlight_place(const QString& path) {
    QPushButton* best = nullptr;
    int longest = 0;
    for (auto* button : place_buttons_) {
        const QString root = button->property("placePath").toString();
        if (root.isEmpty()) {
            continue;
        }
        if (path.startsWith(root, Qt::CaseInsensitive) && root.size() > longest) {
            longest = static_cast<int>(root.size());
            best = button;
        }
    }
    for (auto* button : place_buttons_) {
        const bool on = button == best;
        button->setChecked(on);
        if (auto* glyph = button->findChild<QLabel*>(QStringLiteral("PlaceGlyph"))) {
            Widgets::set_flag(glyph, "on", on);
        }
        if (auto* label = button->findChild<QLabel*>(QStringLiteral("PlaceLabel"))) {
            Widgets::set_flag(label, "on", on);
        }
    }
}

void RemoteFileDialog::add_place_heading(const QString& text) {
    auto* label = Widgets::caption(text);
    label->setContentsMargins(9, places_layout_->count() == 0 ? 0 : 10, 9, 5);
    append_place_widget(label);
}

void RemoteFileDialog::refresh_places() {
    rpc_.request_async(linkproto::cmd::kListDir, {{"path", ""}}, this,
                       [this](const link::AgentReply& reply) {
        const auto drives = reply.ok ? reply.body.value("entries", nlohmann::json::array())
                                     : nlohmann::json::array();
        if (!drives.is_array() || drives.empty() || drives_listed_) {
            return;
        }
        drives_listed_ = true;
        add_place_heading(QStringLiteral("Drives"));
        for (const auto& entry : drives) {
            const auto name = QString::fromStdString(entry.value("name", std::string{}));
            const auto label = QString::fromStdString(entry.value("label", std::string{}));
            add_place(label.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, label),
                      name + QLatin1Char('\\'), QStringLiteral("◔"));
        }
    });
}

QString RemoteFileDialog::suffix_filter() const {
    const QString value = filter_->currentData().toString();
    if (value == QLatin1String("*")) {
        return {};
    }
    return value;
}

QString RemoteFileDialog::parent_of(const QString& path) const {
    QString trimmed = path;
    while (trimmed.endsWith(QLatin1Char('\\'))) {
        trimmed.chop(1);
    }
    const int cut = trimmed.lastIndexOf(QLatin1Char('\\'));
    if (cut <= 2) {
        return trimmed.left(2) + QLatin1Char('\\');
    }
    return trimmed.left(cut);
}

void RemoteFileDialog::set_breadcrumb(const QString& path) {
    while (auto* item = crumbs_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    QString walked;
    const auto parts = path.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        walked += parts.at(i);
        const QString target = walked + QLatin1Char('\\');
        auto* crumb = Widgets::button(parts.at(i), Theme::Role::kQuiet);
        crumb->setCursor(Qt::PointingHandCursor);
        connect(crumb, &QPushButton::clicked, this, [this, target] { navigate_to(target); });
        crumbs_->addWidget(crumb);
        if (i + 1 < parts.size()) {
            crumbs_->addWidget(Widgets::meta(QStringLiteral("\\")));
        }
        walked += QLatin1Char('\\');
    }
}

void RemoteFileDialog::navigate_to(const QString& path) {
    if (!rpc_.has_target()) {
        return;
    }
    current_ = path;
    set_breadcrumb(path);
    highlight_place(path);
    status_->setText(QStringLiteral("Reading %1 over the agent connection").arg(path));
    entries_->clear();

    rpc_.request_async(linkproto::cmd::kListDir,
                       {{"path", path.toStdString()},
                        {"suffix", suffix_filter().toStdString()},
                        {"dirs_only", mode_ == Mode::Directory}},
                       this, [this](const link::AgentReply& reply) { render_entries(reply); });

    if (!drives_listed_) {
        refresh_places();
    }
}

void RemoteFileDialog::render_entries(const link::AgentReply& reply) {
    if (!reply.ok) {
        if (!reply.body.is_object()) {
            status_->setText(QStringLiteral("The guest did not answer (%1). It may be off or "
                                            "still booting.")
                                 .arg(QString::fromStdString(reply.error)));
            return;
        }
        status_->setText(QStringLiteral("%1. The agent runs as the guest's own account.")
                             .arg(QString::fromStdString(reply.error)));
        return;
    }

    const auto entries = reply.body.value("entries", nlohmann::json::array());
    for (const auto& entry : entries) {
        const auto name = QString::fromStdString(entry.value("name", std::string{}));
        const bool is_dir = entry.value("is_dir", false);
        const auto size = entry.value("size", std::int64_t{0});
        const auto mtime = entry.value("mtime", std::int64_t{0});

        auto* item = new EntryItem(entries_, is_dir,
                                   is_dir || size <= 0 ? std::nullopt : std::optional<double>{static_cast<double>(size)},
                                   mtime > 0 ? std::optional<double>{static_cast<double>(mtime)} : std::nullopt);
        item->setText(0, name);
        item->setText(1, is_dir || size <= 0 ? QString{}
                                             : ui::Format::bytes(static_cast<quint64>(size)));
        item->setText(2, mtime > 0 ? QDateTime::fromSecsSinceEpoch(mtime).toString(
                                         QStringLiteral("dd MMM hh:mm"))
                                   : QString{});
        item->setData(0, kRoleIsDir, is_dir);

        QString child = current_;
        if (!child.endsWith(QLatin1Char('\\'))) {
            child += QLatin1Char('\\');
        }
        item->setData(0, kRoleName, child + name);
    }

    ui::TableHeader::refresh(entries_->header());

    const int total = reply.body.value("total", 0);
    const bool more = reply.body.contains("next_cursor");
    status_->setText(more ? QStringLiteral("Showing %1 of %2 entries")
                                .arg(entries.size())
                                .arg(total)
                          : QStringLiteral("%1 entries").arg(entries.size()));
}

void RemoteFileDialog::accept_current() {
    auto* item = entries_->currentItem();
    if (item == nullptr) {
        return;
    }
    QString path = item->data(0, kRoleName).toString();
    if (item->data(0, kRoleIsDir).toBool()) {
        if (mode_ != Mode::Directory) {
            navigate_to(path);
            return;
        }
        selected_ = path;
        accept();
        return;
    }

    if (!path.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
        selected_ = path;
        accept();
        return;
    }

    status_->setText(QStringLiteral("Resolving the shortcut"));
    rpc_.request_async(linkproto::cmd::kResolveShortcut, {{"path", path.toStdString()}}, this,
                       [this, path](const link::AgentReply& reply) {
        const auto field = [&reply](const char* key) {
            return reply.ok ? QString::fromStdString(reply.body.value(key, std::string{}))
                            : QString{};
        };
        const QString target = field("target");
        selected_ = target.isEmpty() ? path : target;
        arguments_ = field("args");
        workdir_ = field("workdir");
        accept();
    });
}

}
