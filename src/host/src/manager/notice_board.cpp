#include "lsw/host/manager/notice_board.hpp"

#include "lsw/host/manager/warning_strip.hpp"

#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

NoticeBoard::NoticeBoard(QWidget* parent) : QWidget(parent), column_(new QVBoxLayout(this)) {
    column_->setContentsMargins(0, 0, 0, 0);
    column_->setSpacing(0);
    hide();
}

void NoticeBoard::post(const QString& key, bool critical, const QString& headline,
                       const QString& detail, const QVector<Action>& actions) {
    withdraw(key);
    auto* strip = new WarningStrip;
    strip->set_message(critical, headline, detail);
    for (const Action& action : actions) {
        connect(strip->add_button(action.text), &QPushButton::clicked, this, action.run);
    }
    strip->add_dismiss();
    connect(strip, &WarningStrip::dismissed, this, [this, key] { withdraw(key); });
    strips_.insert(key, strip);
    column_->insertWidget(0, strip);
    // A child added to a board already on screen stays hidden until shown.
    strip->show();
    show();
}

void NoticeBoard::withdraw(const QString& key) {
    WarningStrip* strip = strips_.take(key);
    if (strip == nullptr) return;
    // A Dismiss click is on the stack: the strip goes after it returns.
    strip->hide();
    strip->deleteLater();
    setVisible(!strips_.isEmpty());
}

}  // namespace lsw::host::manager
