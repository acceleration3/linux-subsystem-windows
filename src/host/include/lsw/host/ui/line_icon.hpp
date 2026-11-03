#pragma once

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QRectF>

#include <array>
#include <cstdint>

class QPainter;

namespace lsw::host::ui {
class LineIcon final {
public:
    enum class Shape : std::uint8_t {
        Overview,
        Display,
        Graphics,
        Files,
        Usb,
        Console,
        Log,
        Maintenance,
        Settings,
        Refresh,
        Plus,
        Hid,
        Disk,
        Count
    };

    LineIcon() = delete;

    static void paint(QPainter& painter, Shape shape, const QRectF& rect, const QColor& color);

    [[nodiscard]] static QIcon icon(Shape shape);

private:
    struct Built {
        QPainterPath stroked;
        QPainterPath filled;
    };

    [[nodiscard]] static const Built& built(Shape shape);
    [[nodiscard]] static Built build(Shape shape);
    [[nodiscard]] static QPainterPath path_data(const char* data);
};

}
