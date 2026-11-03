#include "lsw/host/ui/item_row.hpp"

#include "lsw/host/ui/icon_tile.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::ui {

ItemRow::ItemRow(LineIcon::Shape icon, QWidget* parent) : QWidget(parent) {
    Theme::surface(this, Theme::Role::kItemRow);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(12);

    tile_ = new IconTile(icon);
    row->addWidget(tile_, 0, Qt::AlignVCenter);

    auto* who = new QVBoxLayout;
    who->setSpacing(2);
    name_ = new QLabel;
    name_->setObjectName(QString::fromLatin1(Theme::Role::kItemName));
    name_->setMinimumWidth(0);
    sub_ = Widgets::meta(QString{});
    sub_->setMinimumWidth(0);
    sub_->setVisible(false);
    note_ = new QLabel;
    note_->setObjectName(QString::fromLatin1(Theme::Role::kItemNote));
    note_->setWordWrap(true);
    note_->setVisible(false);
    who->addWidget(name_);
    who->addWidget(sub_);
    who->addWidget(note_);
    row->addLayout(who, 1);

    trailing_ = new QHBoxLayout;
    trailing_->setSpacing(12);
    row->addLayout(trailing_);
}

void ItemRow::set_name(const QString& name) {
    name_->setText(name);
}

void ItemRow::set_sub(const QString& sub) {
    sub_->setText(sub);
    sub_->setVisible(!sub.isEmpty());
}

void ItemRow::set_note(const QString& note, const char* tone) {
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
    Widgets::set_flag(note_, Theme::Tone::kProperty,
                      tone != nullptr ? QVariant(QString::fromLatin1(tone)) : QVariant());
}

void ItemRow::set_dim(bool dim) {
    Widgets::set_flag(name_, Theme::Tone::kProperty, dim ? QVariant(Theme::Tone::kDim) : QVariant());
}

void ItemRow::set_last(bool last) {
    Widgets::set_flag(this, "last", last);
}

}
