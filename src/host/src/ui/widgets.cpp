#include "lsw/host/ui/widgets.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

namespace lsw::host::ui {

namespace {

void repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

}

QLabel* Widgets::caption(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    return label;
}

QLabel* Widgets::meta(const QString& text, bool wrap) {
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    label->setWordWrap(wrap);
    return label;
}

QLabel* Widgets::prose(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kBody));
    label->setWordWrap(true);
    return label;
}

QLabel* Widgets::step_line(const QString& mark, const QString& text, const char* tone) {
    auto* label = new QLabel(QStringLiteral("%1  %2").arg(mark, text));
    label->setWordWrap(true);
    set_flag(label, Theme::Tone::kProperty, tone != nullptr ? QVariant(tone) : QVariant());
    return label;
}

QFrame* Widgets::separator() {
    auto* line = new QFrame;
    line->setObjectName(QString::fromLatin1(Theme::Role::kSeparator));
    line->setFixedHeight(1);
    return line;
}

QPushButton* Widgets::button(const QString& text, const char* role) {
    auto* button = new QPushButton(text);
    if (role != nullptr) {
        button->setObjectName(QString::fromLatin1(role));
    }
    return button;
}

void Widgets::set_role(QWidget* widget, const char* role) {
    widget->setObjectName(QString::fromLatin1(role));
    repolish(widget);
}

void Widgets::set_flag(QWidget* widget, const char* property, const QVariant& value) {
    widget->setProperty(property, value);
    repolish(widget);
}

void Widgets::bring_to_front(QWidget* window) {
    window->show();
    window->raise();
    window->activateWindow();
}

}
