#include "lsw/host/manager/render_panel.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using vm::AreaPressure;
using vm::ChannelPressure;
using K = config::ManagerSettings::Key;

QLabel* caption(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    return label;
}

QLabel* meta(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    return label;
}

const char* colour_for(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return Theme::Color::kBad;
    case AreaPressure::State::Tight:
        return Theme::Color::kAmber;
    case AreaPressure::State::Healthy:
        break;
    }
    return Theme::Color::kOk;
}

QString word_for(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return QStringLiteral("Congested");
    case AreaPressure::State::Tight:
        return QStringLiteral("Tight");
    case AreaPressure::State::Healthy:
        break;
    }
    return QStringLiteral("Healthy");
}

QString size_label(quint64 kib) {
    if (kib >= 1024 && (kib % 1024) == 0) {
        return QStringLiteral("%1 MiB").arg(kib / 1024);
    }
    return QStringLiteral("%1 KiB").arg(kib);
}

QString bytes_label(quint64 bytes) {
    if (bytes >= 1024ULL * 1024ULL) {
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KiB").arg(bytes / 1024);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

// A key/value tile. Several sit in a row under each area's meter.
QWidget* kpi(const QString& key, const QString& value, const char* colour) {
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(11, 9, 11, 9);
    layout->setSpacing(3);
    layout->addWidget(caption(key));
    auto* number = new QLabel(value);
    number->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    number->setStyleSheet(QStringLiteral("font-size: 15px; color: %1;")
                              .arg(QString::fromLatin1(colour)));
    layout->addWidget(number);
    return box;
}

// Which settings key resizes which area, so a suggestion can stage the
// right one.
QString key_for_area(const QString& name) {
    if (name.startsWith(QLatin1String("Command"))) {
        return QLatin1String(K::kRingKib);
    }
    if (name.startsWith(QLatin1String("Reply"))) {
        return QLatin1String(K::kReplyKib);
    }
    return QLatin1String(K::kBlobMib);
}

}  // namespace

RenderPanel::RenderPanel(vm::RenderStats& stats, config::ManagerSettings& settings,
                         config::PendingChanges& pending, QWidget* parent)
    : QWidget(parent), stats_(stats), settings_(settings), pending_(pending) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* strip_holder = new QWidget;
    strip_ = new QHBoxLayout(strip_holder);
    strip_->setContentsMargins(14, 12, 14, 2);
    strip_->setSpacing(8);
    root->addWidget(strip_holder);

    summary_ = meta(QString{});
    summary_->setContentsMargins(14, 8, 14, 2);
    summary_->setWordWrap(true);
    root->addWidget(summary_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    areas_ = new QVBoxLayout(holder);
    areas_->setContentsMargins(14, 12, 14, 14);
    areas_->setSpacing(12);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    connect(&stats_, &vm::RenderStats::updated, this, &RenderPanel::rebuild);
    rebuild();
}

void RenderPanel::select(int channel) {
    selected_ = channel;
    rebuild();
}

void RenderPanel::rebuild() {
    // Both lists are rebuilt wholesale. They are a handful of widgets
    // and they change when a counter moves, which is rare enough that
    // diffing them would be more code than it saves.
    const auto clear = [](QLayout* layout) {
        while (auto* item = layout->takeAt(0)) {
            if (auto* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
    };
    clear(strip_);
    clear(areas_);

    const auto& channels = stats_.channels();
    if (selected_ >= channels.size()) {
        selected_ = 0;
    }

    for (const auto& channel : channels) {
        auto* card = new QPushButton;
        card->setObjectName(QString::fromLatin1(Theme::Role::kNavItem));
        card->setCheckable(true);
        card->setChecked(channel.index == selected_);
        card->setCursor(Qt::PointingHandCursor);
        card->setMinimumWidth(158);

        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(11, 9, 11, 9);
        layout->setSpacing(6);

        auto* top = new QHBoxLayout;
        auto* name = new QLabel(QStringLiteral("Channel %1").arg(channel.index));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* dot = new QLabel(QStringLiteral("●"));
        dot->setAttribute(Qt::WA_TransparentForMouseEvents);
        dot->setStyleSheet(
            QStringLiteral("color: %1;")
                .arg(QString::fromLatin1(channel.claimed ? colour_for(channel.worst())
                                                         : "#39414f")));
        top->addWidget(name);
        top->addStretch(1);
        top->addWidget(dot);
        layout->addLayout(top);

        auto* owner = meta(channel.claimed
                               ? QStringLiteral("pid %1").arg(channel.owner_pid)
                               : QStringLiteral("free"));
        owner->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(owner);

        connect(card, &QPushButton::clicked, this,
                [this, index = channel.index] { select(index); });
        strip_->addWidget(card);
    }
    strip_->addStretch(1);

    if (channels.isEmpty()) {
        return;
    }

    const auto& channel = channels.at(selected_);
    const int region_mib = settings_.int_value(QLatin1String(K::kBlobMib))
                           + settings_.int_value(QLatin1String(K::kRingKib)) / 1024
                           + settings_.int_value(QLatin1String(K::kReplyKib)) / 1024;
    summary_->setText(
        QStringLiteral("Channel %1, about %2 MiB, %3")
            .arg(channel.index)
            .arg(region_mib)
            .arg(channel.claimed
                     ? QStringLiteral("claimed by pid %1").arg(channel.owner_pid)
                     : QStringLiteral("not claimed")));

    for (const auto& area : channel.areas) {
        auto* frame = new QFrame;
        frame->setObjectName(QString::fromLatin1(Theme::Role::kPanel));
        auto* outer = new QVBoxLayout(frame);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto* header = new QWidget;
        header->setObjectName(QString::fromLatin1(Theme::Role::kPanelHeader));
        auto* header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(12, 8, 12, 8);
        header_layout->setSpacing(10);
        auto* name = new QLabel(area.name);
        header_layout->addWidget(name);
        header_layout->addWidget(meta(size_label(area.configured_kib)));
        header_layout->addStretch(1);
        auto* state = new QLabel(word_for(area.state));
        state->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
        state->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(QString::fromLatin1(colour_for(area.state))));
        header_layout->addWidget(state);
        outer->addWidget(header);

        auto* body = new QVBoxLayout;
        body->setContentsMargins(12, 12, 12, 12);
        body->setSpacing(11);

        auto* row = new QHBoxLayout;
        row->setSpacing(1);
        row->addWidget(kpi(QStringLiteral("Events"), QString::number(area.events),
                           area.events > 0 ? colour_for(area.state) : Theme::Color::kFgDim));
        row->addWidget(kpi(QStringLiteral("Extra trips"),
                           QString::number(area.extra_trips), Theme::Color::kFgDim));
        row->addWidget(kpi(QStringLiteral("Largest"), bytes_label(area.largest_bytes),
                           Theme::Color::kFgDim));
        row->addWidget(kpi(QStringLiteral("Suggested"),
                           area.suggested_kib > 0 ? size_label(area.suggested_kib)
                                                  : QStringLiteral("—"),
                           area.suggested_kib > 0 ? Theme::Color::kAmber
                                                  : Theme::Color::kFgFaint));
        row->addStretch(1);
        body->addLayout(row);

        const QString advice = area.advice();
        if (!advice.isEmpty()) {
            auto* advice_row = new QHBoxLayout;
            advice_row->setSpacing(10);
            auto* text = meta(advice);
            text->setWordWrap(true);
            text->setStyleSheet(QStringLiteral("color: #f0c393;"));
            advice_row->addWidget(text, 1);

            auto* stage = new QPushButton(
                QStringLiteral("Stage %1").arg(size_label(area.suggested_kib)));
            const QString key = key_for_area(area.name);
            const int value = key == QLatin1String(K::kBlobMib)
                                  ? static_cast<int>(area.suggested_kib / 1024)
                                  : static_cast<int>(area.suggested_kib);
            connect(stage, &QPushButton::clicked, this, [this, key, value] {
                pending_.stage(key, value);
                Q_EMIT settings_requested();
            });
            advice_row->addWidget(stage);
            body->addLayout(advice_row);
        }

        outer->addLayout(body);
        areas_->addWidget(frame);
    }
    areas_->addStretch(1);
}

}  // namespace lsw::host::manager
