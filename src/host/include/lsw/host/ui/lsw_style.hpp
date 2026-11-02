#pragma once

#include <QProxyStyle>

namespace lsw::host::ui {
class LswStyle : public QProxyStyle {
    Q_OBJECT
public:
    LswStyle();

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget) const override;

    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option,
                                  const QWidget* widget) const override;
};

}
