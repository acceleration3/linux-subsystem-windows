#include "lsw/host/ui/choice_card.hpp"

#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QRadioButton>

namespace lsw::host::ui {

ChoiceCard::ChoiceCard(const QString& title, const QString& description, QWidget* parent)
    : QFrame(parent) {
    Theme::surface(this, Theme::Role::kOptionCard);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(title);
    setAccessibleDescription(description);

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(13, 11, 13, 12);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(2, 1);

    dial_ = new QRadioButton;
    dial_->setAutoExclusive(false);
    dial_->setFocusPolicy(Qt::NoFocus);
    dial_->setAttribute(Qt::WA_TransparentForMouseEvents);
    grid->addWidget(dial_, 0, 0, Qt::AlignVCenter);

    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* name = new QLabel(title);
    name->setObjectName(QString::fromLatin1(Theme::Role::kCardName));
    head->addWidget(name);
    in_use_ = StatusPill::make(StatusPill::State::Done, QStringLiteral("In use"));
    in_use_->hide();
    head->addWidget(in_use_);
    waiting_ = Widgets::tag(true);
    waiting_->hide();
    head->addWidget(waiting_);
    head->addStretch(1);
    grid->addLayout(head, 0, 1);

    grid->addWidget(Widgets::prose(description), 1, 1);
}

void ChoiceCard::set_selected(bool selected) {
    if (selected == dial_->isChecked() && property("selected").toBool() == selected) {
        return;
    }
    dial_->setChecked(selected);
    Widgets::set_flag(this, "selected", selected);
}

bool ChoiceCard::selected() const {
    return dial_->isChecked();
}

void ChoiceCard::set_in_use(bool in_use) {
    in_use_->setVisible(in_use);
}

bool ChoiceCard::in_use() const {
    return !in_use_->isHidden();
}

void ChoiceCard::set_waiting(bool waiting) {
    waiting_->setVisible(waiting);
}

bool ChoiceCard::waiting() const {
    return !waiting_->isHidden();
}

void ChoiceCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        Q_EMIT chosen();
    }
    QFrame::mouseReleaseEvent(event);
}

void ChoiceCard::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter) {
        Q_EMIT chosen();
        return;
    }
    QFrame::keyPressEvent(event);
}

void ChoiceCard::focusInEvent(QFocusEvent* event) {
    QFrame::focusInEvent(event);
    update();
}

void ChoiceCard::focusOutEvent(QFocusEvent* event) {
    QFrame::focusOutEvent(event);
    update();
}

}  // namespace lsw::host::ui
