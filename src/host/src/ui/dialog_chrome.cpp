#include "lsw/host/ui/dialog_chrome.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace lsw::host::ui {

DialogChrome DialogChrome::install(QDialog* dialog) {
    auto* outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    DialogChrome chrome;
    auto* body_widget = new QWidget;
    chrome.body = new QVBoxLayout(body_widget);
    chrome.body->setContentsMargins(20, 18, 20, 16);
    chrome.body->setSpacing(14);
    outer->addWidget(body_widget, 1);

    chrome.foot = foot_band(outer, QMargins(20, 12, 20, 12));
    return chrome;
}

QHBoxLayout* DialogChrome::foot_band(QBoxLayout* into, const QMargins& padding, int spacing) {
    auto* band = new QFrame;
    Theme::surface(band, Theme::Role::kDialogFoot);
    auto* row = new QHBoxLayout(band);
    row->setContentsMargins(padding);
    if (spacing >= 0) {
        row->setSpacing(spacing);
    }
    into->addWidget(band);
    return row;
}

}  // namespace lsw::host::ui
