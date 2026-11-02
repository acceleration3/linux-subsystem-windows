#pragma once

#include <QMargins>

class QBoxLayout;
class QDialog;
class QHBoxLayout;
class QVBoxLayout;

namespace lsw::host::ui {
struct DialogChrome {
    QVBoxLayout* body = nullptr;
    QHBoxLayout* foot = nullptr;

    [[nodiscard]] static DialogChrome install(QDialog* dialog);

    [[nodiscard]] static QHBoxLayout* foot_band(QBoxLayout* into, const QMargins& padding,
                                                int spacing = -1);
};

}
