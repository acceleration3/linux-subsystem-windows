#pragma once

#include <QProxyStyle>

namespace lsw::host::ui {

// The three indicators a stylesheet cannot draw.
//
// Qt stylesheets can colour a check box and a radio button but cannot
// put a tick or a dot inside one, and cannot draw a combo box's caret:
// all three want an image, which means a file, which means a resource
// to install and lose. They are four lines of QPainter each, so they
// are painted here instead, over Fusion, and every other control keeps
// coming from the stylesheet.
class LswStyle : public QProxyStyle {
    Q_OBJECT
public:
    LswStyle();

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget) const override;

    // The indicators are sized here too, because a stylesheet that
    // sized them would also take over drawing them.
    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option,
                                  const QWidget* widget) const override;
};

}  // namespace lsw::host::ui
