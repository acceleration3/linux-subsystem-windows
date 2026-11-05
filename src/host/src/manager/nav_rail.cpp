#include "lsw/host/manager/nav_rail.hpp"

#include "lsw/host/manager/nav_icon.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;

constexpr int kPageCount = static_cast<int>(NavRail::Page::Count);

using Shape = ui::LineIcon::Shape;

struct ItemSpec {
    NavRail::Page page;
    Shape icon;
    const char* label;
    bool rule_after;
};

constexpr std::array<ItemSpec, kPageCount> kItems{{
    {NavRail::Page::Overview, Shape::Overview, "Overview", true},
    {NavRail::Page::Display, Shape::Display, "Display", false},
    {NavRail::Page::Graphics, Shape::Graphics, "Graphics", false},
    {NavRail::Page::GpuStatus, Shape::GpuStatus, "GPU status", false},
    {NavRail::Page::Files, Shape::Files, "Files", false},
    {NavRail::Page::Usb, Shape::Usb, "USB", true},
    {NavRail::Page::Console, Shape::Console, "Console", false},
    {NavRail::Page::Log, Shape::Log, "Log", false},
    {NavRail::Page::Maintenance, Shape::Maintenance, "Maintenance", true},
    {NavRail::Page::Settings, Shape::Settings, "Settings", false},
}};

}

NavRail::NavRail(QWidget* parent) : QWidget(parent) {
    Theme::surface(this, Theme::Role::kNavRail);
    setFixedWidth(186);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

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
    detail_label_->setStyleSheet(QStringLiteral("font-size: 10px;"));

    id_layout->addWidget(name_label_);
    id_layout->addWidget(detail_label_);
    root->addWidget(identity);

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

        auto* row = new QHBoxLayout(button);
        row->setContentsMargins(9, 4, 9, 4);
        row->setSpacing(10);

        auto* icon = new NavIcon(spec.icon);

        auto* label = new QLabel(QString::fromLatin1(spec.label));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto* marker = Widgets::meta(QString{});
        marker->setVisible(false);
        marker->setAttribute(Qt::WA_TransparentForMouseEvents);

        row->addWidget(icon);
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

        if (spec.rule_after) {
            auto* gap = new QWidget;
            auto* gap_layout = new QVBoxLayout(gap);
            gap_layout->setContentsMargins(8, 5, 8, 5);
            auto* rule = new QFrame;
            rule->setObjectName(QString::fromLatin1(Theme::Role::kNavRule));
            rule->setFixedHeight(1);
            gap_layout->addWidget(rule);
            items_layout->addWidget(gap);
        }
    }
    root->addWidget(items);
    root->addStretch(1);

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

void NavRail::set_marker(Page page, const QString& text, const char* tone) {
    const int index = static_cast<int>(page);
    if (index < 0 || index >= kPageCount) {
        return;
    }
    state_[static_cast<std::size_t>(index)].text = text;
    state_[static_cast<std::size_t>(index)].tone = tone;
    show_marker(page);
}

void NavRail::show_marker(Page page) {
    const int index = static_cast<int>(page);
    const Marker& state = state_[static_cast<std::size_t>(index)];
    const bool pending = state.pending > 0;
    const QString text = pending ? QString::number(state.pending) : state.text;
    const char* tone = pending ? Theme::Tone::kAmber : state.tone;
    auto* marker = markers_[index];
    marker->setText(text);
    marker->setVisible(!text.isEmpty());
    Widgets::set_flag(marker, Theme::Tone::kProperty,
                      tone != nullptr ? QVariant(QString::fromLatin1(tone)) : QVariant());
}

void NavRail::set_guest_provisioned(bool provisioned) {
    for (const auto& spec : kItems) {
        if (spec.page == Page::Console || spec.page == Page::Graphics ||
            spec.page == Page::GpuStatus) {
            items_[static_cast<int>(spec.page)]->setEnabled(provisioned);
        }
    }
    power_->setEnabled(provisioned);
}

void NavRail::set_log_count(int records) {
    set_marker(Page::Log, records > 0 ? QString::number(records) : QString{}, nullptr);
}

void NavRail::set_usb_count(int devices) {
    set_marker(Page::Usb, devices > 0 ? QString::number(devices) : QString{}, nullptr);
}

void NavRail::set_pending(Page page, int changes) {
    const int index = static_cast<int>(page);
    if (index < 0 || index >= kPageCount) {
        return;
    }
    state_[static_cast<std::size_t>(index)].pending = changes;
    show_marker(page);
}

void NavRail::set_graphics_warning(const char* tone) {
    set_marker(Page::Graphics, tone != nullptr ? QStringLiteral("●") : QString{}, tone);
}

void NavRail::set_gpu_status_warning(const char* tone) {
    set_marker(Page::GpuStatus, tone != nullptr ? QStringLiteral("●") : QString{}, tone);
}

}
