#include "lsw/host/ui/key_values.hpp"

#include "lsw/host/ui/info_tip.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::ui {

namespace {

bool warns(const char* tone) {
    return tone != nullptr && (qstrcmp(tone, Theme::Tone::kBad) == 0 || qstrcmp(tone, Theme::Tone::kAmber) == 0);
}

QWidget* key_cell(const QString& key, const QString& about) {
    auto* cell = new QWidget;
    auto* line = new QHBoxLayout(cell);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(6);
    line->addWidget(Widgets::caption(key));
    line->addWidget(new InfoTip(about));
    line->addStretch(1);
    return cell;
}

}

KeyValues::KeyValues(QVBoxLayout* body) : grid_(new QGridLayout) {
    grid_->setHorizontalSpacing(16);
    grid_->setVerticalSpacing(9);
    grid_->setColumnStretch(1, 1);
    body->addLayout(grid_);
}

QHBoxLayout* KeyValues::add(const QString& key, const QString& value, const char* tone,
                            const QString& note) {
    auto* line = new QHBoxLayout;
    line->setSpacing(8);
    auto* shown = new QLabel(value);
    if (tone != nullptr) {
        shown->setProperty(Theme::Tone::kProperty, QString::fromLatin1(tone));
    }
    line->addWidget(shown);
    const bool warning = warns(tone) && !note.isEmpty();
    if (warning) {
        line->addWidget(Widgets::meta(note, true), 1);
    } else {
        line->addStretch(1);
    }
    grid_->addWidget(key_cell(key, warning ? QString{} : note), row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid_->addLayout(line, row_, 1);
    ++row_;
    return line;
}

QHBoxLayout* KeyValues::add_widget(const QString& key, QWidget* value, const QString& about) {
    auto* line = new QHBoxLayout;
    line->setSpacing(10);
    line->addWidget(value);
    line->addStretch(1);
    grid_->addWidget(key_cell(key, about), row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid_->addLayout(line, row_, 1);
    ++row_;
    return line;
}

}
