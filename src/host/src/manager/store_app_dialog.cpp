#include "lsw/host/manager/store_app_dialog.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>

namespace lsw::host::manager {

using ui::Theme;
using ui::Widgets;

namespace {
constexpr int kIconPx = 28;
// Read at twice the row's size, so a HiDPI screen draws it sharp.
constexpr int kListIconPx = 2 * kIconPx;
constexpr int kRowPx = 44;
constexpr int kRowInset = 16;
constexpr int kIconGap = 11;
constexpr int kSelectedRule = 2;
constexpr int kFamilyRole = Qt::UserRole + 1;
constexpr int kAumidRole = Qt::UserRole;
constexpr const char* kSelectedGround = "#241d14";
constexpr const char* kHoverGround = Theme::Color::kRaised;
}  // namespace

// A row as the design draws it: the icon, the name, and the package family
// under it in mono, so two of a name can be told apart.
class StoreAppDialog::RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        p->save();
        const QRect row = option.rect;
        if (option.state & QStyle::State_Selected) {
            p->fillRect(row, Theme::color(kSelectedGround));
            p->fillRect(QRect(row.left(), row.top(), kSelectedRule, row.height()),
                        Theme::color(Theme::Color::kAmber));
        } else if (option.state & QStyle::State_MouseOver) {
            p->fillRect(row, Theme::color(kHoverGround));
        }
        const QRect icon(row.left() + kRowInset, row.center().y() - kIconPx / 2 + 1, kIconPx, kIconPx);
        index.data(Qt::DecorationRole).value<QIcon>().paint(p, icon);

        const int text_left = icon.right() + 1 + kIconGap;
        const QRect text(text_left, row.top(), row.right() - kRowInset - text_left, row.height());
        QFont name_font = option.font;
        QFont family_font = Theme::mono_font(10);
        const QFontMetrics name_metrics(name_font);
        const QFontMetrics family_metrics(family_font);
        const int block = name_metrics.height() + 1 + family_metrics.height();
        const int top = text.top() + (text.height() - block) / 2;
        p->setFont(name_font);
        p->setPen(Theme::color(Theme::Color::kFg));
        p->drawText(QRect(text.left(), top, text.width(), name_metrics.height()), Qt::AlignLeft,
                    name_metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                            text.width()));
        p->setFont(family_font);
        p->setPen(Theme::color(Theme::Color::kFgFaint));
        // Taller than the line by the underline's reach: package families
        // are full of underscores, and the line's own box clips them.
        p->drawText(QRect(text.left(), top + name_metrics.height() + 1, text.width(),
                          family_metrics.height() + family_metrics.underlinePos() + 1),
                    Qt::AlignLeft,
                    family_metrics.elidedText(index.data(kFamilyRole).toString(), Qt::ElideRight,
                                              text.width()));
        p->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override {
        return {QStyledItemDelegate::sizeHint(option, index).width(), kRowPx};
    }
};

StoreAppDialog::StoreAppDialog(link::AgentRpcClient& rpc, QWidget* parent)
    : QDialog(parent), search_(new QLineEdit), pages_(new QStackedWidget), list_(new QListWidget),
      reading_(ui::StatusPill::make(ui::StatusPill::State::Running, QStringLiteral("Reading"))),
      offline_(new QLabel(QStringLiteral("○  OFFLINE"))), empty_text_(new QLabel),
      count_(Widgets::meta(QString{})), add_(Widgets::button(QStringLiteral("Add"), Theme::Role::kPrimary)) {
    setWindowTitle(QStringLiteral("Add a store app"));
    setModal(true);
    resize(560, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* search_row = new QVBoxLayout;
    search_row->setContentsMargins(20, 18, 20, 12);
    search_->setPlaceholderText(QStringLiteral("Search by name or package"));
    search_->setClearButtonEnabled(true);
    search_row->addWidget(search_);
    root->addLayout(search_row);

    list_->setObjectName(QStringLiteral("StoreApps"));
    list_->setItemDelegate(new RowDelegate(list_));
    list_->setMouseTracking(true);
    list_->setUniformItemSizes(true);
    list_->setIconSize({kIconPx, kIconPx});
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pages_->addWidget(list_);

    auto* empty = new QWidget;
    Theme::surface(empty, "StoreAppsEmpty");
    auto* empty_layout = new QVBoxLayout(empty);
    empty_layout->setContentsMargins(24, 24, 24, 24);
    empty_layout->setSpacing(10);
    empty_layout->addStretch(1);
    Widgets::set_role(offline_, Theme::Role::kBadgeOffline);
    empty_layout->addWidget(reading_, 0, Qt::AlignHCenter);
    empty_layout->addWidget(offline_, 0, Qt::AlignHCenter);
    empty_text_->setWordWrap(true);
    empty_text_->setAlignment(Qt::AlignCenter);
    empty_text_->setTextFormat(Qt::PlainText);
    Widgets::set_role(empty_text_, Theme::Role::kDim);
    empty_layout->addWidget(empty_text_);
    empty_layout->addStretch(1);
    pages_->addWidget(empty);
    root->addWidget(pages_, 1);

    auto* foot = ui::DialogChrome::foot_band(root, QMargins(20, 12, 20, 12), 8);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    add_->setDefault(true);
    add_->setEnabled(false);
    foot->addWidget(count_);
    foot->addStretch(1);
    foot->addWidget(cancel);
    foot->addWidget(add_);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(add_, &QPushButton::clicked, this, &StoreAppDialog::choose);
    connect(list_, &QListWidget::itemDoubleClicked, this, &StoreAppDialog::choose);
    connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current) { add_->setEnabled(current != nullptr); });
    connect(search_, &QLineEdit::textChanged, this, &StoreAppDialog::filter);
    // The search keeps the focus; the arrows still walk the list.
    for (const auto key : {Qt::Key_Down, Qt::Key_Up}) {
        auto* walk = new QShortcut(QKeySequence(key), search_, nullptr, nullptr, Qt::WidgetShortcut);
        connect(walk, &QShortcut::activated, this, [this, key] {
            const int row = list_->currentRow() + (key == Qt::Key_Down ? 1 : -1);
            for (int r = row; r >= 0 && r < list_->count(); r += (key == Qt::Key_Down ? 1 : -1)) {
                if (!list_->item(r)->isHidden()) {
                    list_->setCurrentRow(r);
                    return;
                }
            }
        });
    }
    search_->setFocus();

    show_empty(reading_, QStringLiteral("Asking Windows for its store apps and their icons"));
    count_->setText(QString{});
    rpc.request_async(linkproto::cmd::kListStoreApps, {{"icon_size", kListIconPx}}, this,
                      [this](const link::AgentReply& reply) { read(reply); });
}

void StoreAppDialog::read(const link::AgentReply& reply) {
    if (!reply.ok) {
        show_empty(offline_, QStringLiteral("Windows did not answer, so there is nothing to list: %1")
                                 .arg(QString::fromStdString(reply.error)));
        return;
    }
    for (const auto& app : reply.body.value("apps", nlohmann::json::array())) {
        StoreApp entry{QString::fromStdString(app.value("aumid", std::string{})),
                       QString::fromStdString(app.value("name", std::string{})),
                       QString::fromStdString(app.value("family", std::string{})),
                       QString::fromStdString(app.value("icon_png_base64", std::string{}))};
        if (entry.aumid.isEmpty() || entry.name.isEmpty()) continue;
        QPixmap icon;
        icon.loadFromData(QByteArray::fromBase64(entry.icon_png_base64.toLatin1()), "PNG");
        auto* item = new QListWidgetItem(QIcon(icon), entry.name, list_);
        item->setData(kAumidRole, entry.aumid);
        item->setData(kFamilyRole, entry.family);
        item->setToolTip(entry.aumid);
        apps_.push_back(std::move(entry));
    }
    if (apps_.isEmpty()) {
        show_empty(nullptr, QStringLiteral("This Windows has no apps from the Microsoft Store."));
        return;
    }
    filter();
}

void StoreAppDialog::filter() {
    if (apps_.isEmpty()) return;
    const QString needle = search_->text().trimmed();
    int shown = 0;
    QListWidgetItem* first = nullptr;
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        const bool match = needle.isEmpty() ||
                           item->text().contains(needle, Qt::CaseInsensitive) ||
                           item->data(kFamilyRole).toString().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (!match) continue;
        ++shown;
        if (first == nullptr) first = item;
    }
    count_->setText(shown == list_->count()
                        ? QStringLiteral("%1 store apps").arg(shown)
                        : QStringLiteral("%1 of %2 store apps").arg(shown).arg(list_->count()));
    if (shown == 0) {
        list_->setCurrentItem(nullptr);
        show_empty(nullptr, QStringLiteral("No store app is called “%1”").arg(needle));
        return;
    }
    if (list_->currentItem() == nullptr || list_->currentItem()->isHidden()) {
        list_->setCurrentItem(first);
    }
    show_list();
}

void StoreAppDialog::show_list() {
    pages_->setCurrentWidget(list_);
}

void StoreAppDialog::show_empty(QWidget* mark, const QString& text) {
    reading_->setVisible(mark == reading_);
    offline_->setVisible(mark == offline_);
    empty_text_->setText(text);
    pages_->setCurrentIndex(1);
    add_->setEnabled(false);
}

void StoreAppDialog::choose() {
    const auto* item = list_->currentItem();
    if (item == nullptr || item->isHidden() || pages_->currentWidget() != list_) return;
    const QString aumid = item->data(kAumidRole).toString();
    for (const StoreApp& app : apps_) {
        if (app.aumid != aumid) continue;
        chosen_ = {};
        chosen_.aumid = app.aumid;
        chosen_.name = app.name;
        chosen_.icon_png_base64 = app.icon_png_base64;
        chosen_.id = apps::AppConfig::make_id(app.aumid);
        accept();
        return;
    }
}

}  // namespace lsw::host::manager
