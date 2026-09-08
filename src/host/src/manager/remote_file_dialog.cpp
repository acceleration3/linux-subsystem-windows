#include "lsw/host/manager/remote_file_dialog.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QButtonGroup>
#include <QComboBox>
#include <QHeaderView>
#include <QPointer>
#include <QStyle>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

using ui::Theme;

constexpr int kRoleName = Qt::UserRole;
constexpr int kRoleIsDir = Qt::UserRole + 1;

// Where applications actually are. The first entry is where the dialog
// opens, because it is the same folder the application catalog already
// enumerates: picking one is two clicks rather than a remembered path.
struct Place {
    const char* label;
    const char* path;
    const char* glyph;
};

// The glyphs are the two a plain desktop font is certain to have. An
// icon theme would be prettier and is not guaranteed to be installed.
const Place kPlaces[] = {
    {"Start menu", R"(C:\ProgramData\Microsoft\Windows\Start Menu\Programs)", "▣"},
    {"Program Files", R"(C:\Program Files)", "▢"},
    {"Program Files (x86)", R"(C:\Program Files (x86))", "▢"},
    {"Desktop", R"(C:\Users\Public\Desktop)", "●"},
    {"Downloads", R"(C:\Users\Public\Downloads)", "●"},
};

QString human_size(qint64 bytes) {
    if (bytes <= 0) {
        return {};
    }
    if (bytes >= 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 1);
    }
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    }
    return QStringLiteral("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
}

}  // namespace

RemoteFileDialog::RemoteFileDialog(link::AgentRpcClient& rpc, Mode mode, QWidget* parent)
    : QDialog(parent), rpc_(rpc), mode_(mode) {
    setWindowTitle(mode_ == Mode::Directory
                       ? QStringLiteral("Choose a folder in Windows")
                       : QStringLiteral("Choose a program in Windows"));
    setModal(true);
    resize(780, 560);
    build();

    if (!rpc_.has_target()) {
        status_->setText(QStringLiteral("Windows is not running. There is nothing to "
                                        "browse until the guest is up."));
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

    // ---- places ---------------------------------------------------
    // A column of buttons rather than a list: the design gives the
    // current place an amber edge and its glyph the same colour, and a
    // list item is one piece of text that cannot be two colours.
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

    // ---- pane -----------------------------------------------------
    auto* pane = new QWidget;
    auto* pane_layout = new QVBoxLayout(pane);
    pane_layout->setContentsMargins(0, 0, 0, 0);
    pane_layout->setSpacing(0);

    auto* bar = new QWidget;
    Theme::surface(bar, Theme::Role::kContentBar);
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(10, 8, 10, 8);
    bar_layout->setSpacing(6);

    auto* up = new QPushButton(QStringLiteral("↑"));
    up->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
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
    entries_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    entries_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    entries_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    entries_->setColumnWidth(1, 96);
    entries_->setColumnWidth(2, 140);
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

    // ---- name and filter ------------------------------------------
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

    // ---- footer ---------------------------------------------------
    auto* footer = new QFrame;
    Theme::surface(footer, Theme::Role::kDialogFoot);
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(12, 10, 12, 10);
    footer_layout->setSpacing(8);
    status_ = new QLabel;
    status_->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    status_->setWordWrap(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    choose_button_ = new QPushButton(QStringLiteral("Choose"));
    choose_button_->setObjectName(QString::fromLatin1(Theme::Role::kPrimary));
    footer_layout->addWidget(status_, 1);
    footer_layout->addWidget(cancel);
    footer_layout->addWidget(choose_button_);
    pane_layout->addWidget(footer);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(choose_button_, &QPushButton::clicked, this,
            &RemoteFileDialog::accept_current);

    root->addWidget(pane, 1);
}

// The column ends in a stretch once it has been built, and anything
// added later (the drives, when the guest answers) goes above it.
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
    // The row height is in the stylesheet, not here: QStyleSheetStyle
    // rewrites a widget's minimum and maximum size from the sheet when
    // it polishes it, so setFixedHeight on a styled button is undone
    // and the button comes back the height of its (empty) text.

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

// Which place the pane is inside. A folder deeper in a place still
// belongs to it, so the longest matching prefix wins.
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
            glyph->setProperty("on", on);
            glyph->style()->unpolish(glyph);
            glyph->style()->polish(glyph);
        }
        if (auto* label = button->findChild<QLabel*>(QStringLiteral("PlaceLabel"))) {
            label->setProperty("on", on);
            label->style()->unpolish(label);
            label->style()->polish(label);
        }
    }
}

// A heading over a group of places. Small mono caps, and the design's
// breathing room above it, because it separates two kinds of thing.
void RemoteFileDialog::add_place_heading(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    label->setContentsMargins(9, places_layout_->count() == 0 ? 0 : 10, 9, 5);
    append_place_widget(label);
}

void RemoteFileDialog::refresh_places() {
    // The drives the guest reports, appended under the fixed shortcuts.
    // A mapped host folder is a drive letter in there like any other,
    // which is exactly how it should read.
    const QPointer<RemoteFileDialog> self{this};
    (void)QtConcurrent::run([self]() {
        if (!self) {
            return;
        }
        const nlohmann::json request = {{"cmd", linkproto::cmd::kListDir},
                                        {"params", {{"path", ""}}}};
        const auto raw = self->rpc_.call(request);
        QMetaObject::invokeMethod(self.data(), [self, raw] {
            if (!self || raw.isEmpty()) {
                return;
            }
            const auto reply = QJsonDocument::fromJson(raw).object();
            const auto drives = reply.value(QStringLiteral("entries")).toArray();
            if (drives.isEmpty() || self->drives_listed_) {
                return;
            }
            self->drives_listed_ = true;
            self->add_place_heading(QStringLiteral("Drives"));
            for (const auto value : drives) {
                const auto entry = value.toObject();
                const QString name = entry.value(QStringLiteral("name")).toString();
                const QString label = entry.value(QStringLiteral("label")).toString();
                self->add_place(label.isEmpty() ? name
                                                : QStringLiteral("%1 %2").arg(name, label),
                                name + QLatin1Char('\\'), QStringLiteral("◔"));
            }
        });
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
        // "C:" is as far up as this goes; above that is the drive list.
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
        auto* crumb = new QPushButton(parts.at(i));
        crumb->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
        crumb->setCursor(Qt::PointingHandCursor);
        connect(crumb, &QPushButton::clicked, this, [this, target] { navigate_to(target); });
        crumbs_->addWidget(crumb);
        if (i + 1 < parts.size()) {
            auto* sep = new QLabel(QStringLiteral("\\"));
            sep->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
            crumbs_->addWidget(sep);
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

    const QPointer<RemoteFileDialog> self{this};
    const std::string wanted = path.toStdString();
    const std::string suffix = suffix_filter().toStdString();
    const bool dirs_only = mode_ == Mode::Directory;
    (void)QtConcurrent::run([self, wanted, suffix, dirs_only]() {
        if (!self) {
            return;
        }
        const nlohmann::json request = {
            {"cmd", linkproto::cmd::kListDir},
            {"params",
             {{"path", wanted}, {"suffix", suffix}, {"dirs_only", dirs_only}}},
        };
        const auto raw = self->rpc_.call(request);
        QMetaObject::invokeMethod(self.data(), [self, raw] {
            if (!self) {
                return;
            }
            if (raw.isEmpty()) {
                self->status_->setText(
                    QStringLiteral("The guest did not answer. It may still be booting."));
                return;
            }
            self->render_entries(QJsonDocument::fromJson(raw).object());
        });
    });

    if (!drives_listed_) {
        refresh_places();
    }
}

void RemoteFileDialog::render_entries(const QJsonObject& reply) {
    const QString error = reply.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        // The agent runs as the wizard user, so a folder belonging to
        // somebody else is refused by Windows and not by us. Saying
        // which is the difference between a bug and a permission.
        status_->setText(QStringLiteral("%1. The agent runs as the guest's own account.")
                             .arg(error));
        return;
    }

    const auto entries = reply.value(QStringLiteral("entries")).toArray();
    for (const auto value : entries) {
        const auto entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const bool is_dir = entry.value(QStringLiteral("is_dir")).toBool();
        const auto size = static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble());
        const auto mtime =
            static_cast<qint64>(entry.value(QStringLiteral("mtime")).toDouble());

        auto* item = new QTreeWidgetItem(entries_);
        item->setText(0, name);
        item->setText(1, is_dir ? QString{} : human_size(size));
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

    const int total = reply.value(QStringLiteral("total")).toInt();
    const bool more = reply.contains(QStringLiteral("next_cursor"));
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

    // A shortcut is not the thing to store: an uninstaller deletes it
    // and the entry stops working for a reason nobody can see. Ask the
    // guest what it points at, and keep its arguments and working
    // folder while we are there.
    status_->setText(QStringLiteral("Resolving the shortcut"));
    const QPointer<RemoteFileDialog> self{this};
    const std::string wanted = path.toStdString();
    (void)QtConcurrent::run([self, wanted, path]() {
        if (!self) {
            return;
        }
        const nlohmann::json request = {{"cmd", linkproto::cmd::kResolveShortcut},
                                        {"params", {{"path", wanted}}}};
        const auto raw = self->rpc_.call(request);
        QMetaObject::invokeMethod(self.data(), [self, raw, path] {
            if (!self) {
                return;
            }
            const auto reply = QJsonDocument::fromJson(raw).object();
            const QString target = reply.value(QStringLiteral("target")).toString();
            // A shortcut that resolves to nothing is still better stored
            // as itself than not stored at all.
            self->selected_ = target.isEmpty() ? path : target;
            self->arguments_ = reply.value(QStringLiteral("args")).toString();
            self->workdir_ = reply.value(QStringLiteral("workdir")).toString();
            self->accept();
        });
    });
}

}  // namespace lsw::host::manager
