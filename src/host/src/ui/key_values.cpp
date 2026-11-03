#include "lsw/host/ui/key_values.hpp"

#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::ui {

namespace {

QLabel* note_label(const QString& note) {
    return Widgets::meta(note, true);
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
    line->setSpacing(6);
    if (value == QLatin1String("-") && !note.isEmpty()) {
        line->addWidget(note_label(note), 1);
    } else {
        auto* shown = new QLabel(value);
        if (tone != nullptr) {
            shown->setProperty(Theme::Tone::kProperty, QString::fromLatin1(tone));
        }
        line->addWidget(shown);
        if (!note.isEmpty()) {
            line->addWidget(note_label(note), 1);
        } else {
            line->addStretch(1);
        }
    }
    grid_->addWidget(Widgets::caption(key), row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid_->addLayout(line, row_, 1);
    ++row_;
    return line;
}

QHBoxLayout* KeyValues::add_widget(const QString& key, QWidget* value) {
    auto* line = new QHBoxLayout;
    line->setSpacing(10);
    line->addWidget(value);
    line->addStretch(1);
    grid_->addWidget(Widgets::caption(key), row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid_->addLayout(line, row_, 1);
    ++row_;
    return line;
}

}
