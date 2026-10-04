#include "lsw/host/manager/nav_rail.hpp"

#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;

constexpr int kPageCount = static_cast<int>(NavRail::Page::Count);

struct ItemSpec {
    NavRail::Page page;
    const char* glyph;
    const char* label;
};

// The glyphs are plain text rather than an icon font: they have to
// survive a host with no icon theme, and every one of them is in the
// fonts a desktop already has.
constexpr ItemSpec kItems[kPageCount] = {
    {NavRail::Page::Overview, "□", "Overview"},
    {NavRail::Page::Console, ">_", "Console"},
    {NavRail::Page::Log, "≡", "Log"},
    {NavRail::Page::Render, "◎", "Render"},
    {NavRail::Page::Settings, "⚙", "Settings"},
};

}  // namespace

NavRail::NavRail(QWidget* parent) : QWidget(parent) {
    Theme::surface(this, Theme::Role::kNavRail);
    setFixedWidth(186);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- identity -------------------------------------------------
    auto* identity = new QWidget;
    Theme::surface(identity, Theme::Role::kNavIdentity);
    auto* id_layout = new QVBoxLayout(identity);
    id_layout->setContentsMargins(14, 13, 14, 12);
    id_layout->setSpacing(2);

    name_label_ = new QLabel;
    QFont name_font = name_label_->font();
    name_font.setPixelSize(15);
    name_font.setWeight(QFont::DemiBold);
    name_label_->setFont(name_font);

    detail_label_ = Widgets::meta(QString{}, true);
    // A shade smaller than the other captions: the machine's shape is
    // three facts on one line, and wrapping "60 GiB" onto a third line
    // makes the identity look like an error.
    detail_label_->setStyleSheet(QStringLiteral("font-size: 10px;"));

    id_layout->addWidget(name_label_);
    id_layout->addWidget(detail_label_);
    root->addWidget(identity);

    // ---- items ----------------------------------------------------
    auto* items = new QWidget;
    auto* items_layout = new QVBoxLayout(items);
    items_layout->setContentsMargins(8, 8, 8, 8);
    items_layout->setSpacing(1);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);

    for (const auto& spec : kItems) {
        const int index = static_cast<int>(spec.page);

        auto* button = new QPushButton;
        button->setObjectName(QString::fromLatin1(Theme::Role::kNavItem));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);

        // The row is laid out inside the button rather than written as
        // its text, because the marker has to sit hard right whatever
        // the label's length is.
        auto* row = new QHBoxLayout(button);
        row->setContentsMargins(9, 4, 9, 4);
        row->setSpacing(10);

        auto* glyph = Widgets::meta(QString::fromUtf8(spec.glyph));
        glyph->setFixedWidth(15);
        glyph->setAlignment(Qt::AlignCenter);
        glyph->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto* label = new QLabel(QString::fromLatin1(spec.label));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto* marker = Widgets::meta(QString{});
        marker->setVisible(false);
        marker->setAttribute(Qt::WA_TransparentForMouseEvents);

        row->addWidget(glyph);
        row->addWidget(label);
        row->addStretch(1);
        row->addWidget(marker);

        group_->addButton(button, index);
        items_layout->addWidget(button);
        items_[index] = button;
        markers_[index] = marker;

        connect(button, &QPushButton::clicked, this, [this, page = spec.page] {
            current_ = page;
            Q_EMIT page_selected(page);
        });
    }
    root->addWidget(items);
    root->addStretch(1);

    // ---- power ----------------------------------------------------
    auto* power_box = new QWidget;
    Theme::surface(power_box, Theme::Role::kNavPower);
    auto* power_layout = new QVBoxLayout(power_box);
    power_layout->setContentsMargins(12, 11, 12, 12);
    power_layout->setSpacing(9);

    badge_ = new QLabel;
    badge_->setAlignment(Qt::AlignCenter);

    power_ = new QPushButton;
    connect(power_, &QPushButton::clicked, this, [this] {
        if (running_) {
            Q_EMIT stop_requested();
        } else {
            Q_EMIT start_requested();
        }
    });

    power_layout->addWidget(badge_);
    power_layout->addWidget(power_);
    root->addWidget(power_box);

    set_identity(QStringLiteral("lsw"), QString{});
    set_agent_online(false);
    set_running(false);
    set_current(Page::Overview);
}

void NavRail::set_identity(const QString& name, const QString& detail) {
    name_label_->setText(name);
    detail_label_->setText(detail);
    detail_label_->setVisible(!detail.isEmpty());
}

void NavRail::set_current(Page page) {
    const int index = static_cast<int>(page);
    if (index < 0 || index >= kPageCount) {
        return;
    }
    current_ = page;
    items_[index]->setChecked(true);
}

void NavRail::set_agent_online(bool online) {
    badge_->setText(online ? QStringLiteral("\u25cf  ONLINE") : QStringLiteral("\u25cb  OFFLINE"));
    Widgets::set_role(badge_, online ? Theme::Role::kBadgeOnline : Theme::Role::kBadgeOffline);
}

void NavRail::set_running(bool running) {
    running_ = running;
    power_->setText(running ? QStringLiteral("Stop the guest")
                            : QStringLiteral("Start the guest"));
    Widgets::set_role(power_, running ? Theme::Role::kStop : Theme::Role::kGo);
}

void NavRail::set_marker(Page page, const QString& text, bool amber) {
    const int index = static_cast<int>(page);
    if (index < 0 || index >= kPageCount) {
        return;
    }
    auto* marker = markers_[index];
    marker->setText(text);
    marker->setVisible(!text.isEmpty());
    Widgets::set_flag(marker, Theme::Tone::kProperty,
                      amber ? QVariant(Theme::Tone::kAmber) : QVariant());
}

void NavRail::set_guest_provisioned(bool provisioned) {
    for (const auto page : {Page::Console, Page::Log, Page::Render}) {
        items_[static_cast<int>(page)]->setEnabled(provisioned);
    }
    power_->setEnabled(provisioned);
}

void NavRail::set_log_count(int records) {
    set_marker(Page::Log, records > 0 ? QString::number(records) : QString{}, false);
}

void NavRail::set_pending_changes(int count) {
    set_marker(Page::Settings, count > 0 ? QString::number(count) : QString{}, true);
}

void NavRail::set_render_warning(bool warned) {
    // A dot rather than a number: the count of congested areas is not
    // the interesting part, the fact that any of them is congested is.
    set_marker(Page::Render, warned ? QStringLiteral("●") : QString{}, true);
}

}  // namespace lsw::host::manager
