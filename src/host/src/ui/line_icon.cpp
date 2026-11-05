#include "lsw/host/ui/line_icon.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QIconEngine>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <cstring>
#include <numbers>

namespace lsw::host::ui {

namespace {

constexpr qreal kViewBox = 20.0;
constexpr qreal kStroke = 1.5;

void svg_arc(QPainterPath& path, qreal rx, qreal ry, qreal rotation_deg, bool large, bool sweep,
             qreal x, qreal y) {
    const QPointF from = path.currentPosition();
    if (from == QPointF(x, y)) {
        return;
    }
    if (rx == 0 || ry == 0) {
        path.lineTo(x, y);
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    const qreal phi = rotation_deg * std::numbers::pi / 180.0;
    const qreal cos_phi = std::cos(phi);
    const qreal sin_phi = std::sin(phi);
    const qreal dx = (from.x() - x) / 2.0;
    const qreal dy = (from.y() - y) / 2.0;
    const qreal x1 = cos_phi * dx + sin_phi * dy;
    const qreal y1 = -sin_phi * dx + cos_phi * dy;
    const qreal lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1.0) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const qreal numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const qreal denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    qreal factor = denominator == 0 ? 0 : std::sqrt(std::max(0.0, numerator / denominator));
    if (large == sweep) {
        factor = -factor;
    }
    const qreal cx1 = factor * (rx * y1 / ry);
    const qreal cy1 = factor * -(ry * x1 / rx);
    const qreal cx = cos_phi * cx1 - sin_phi * cy1 + (from.x() + x) / 2.0;
    const qreal cy = sin_phi * cx1 + cos_phi * cy1 + (from.y() + y) / 2.0;
    const auto angle = [](qreal ux, qreal uy, qreal vx, qreal vy) {
        const qreal dot = ux * vx + uy * vy;
        const qreal length = std::hypot(ux, uy) * std::hypot(vx, vy);
        qreal value = std::acos(std::clamp(dot / length, -1.0, 1.0));
        if (ux * vy - uy * vx < 0) {
            value = -value;
        }
        return value;
    };
    const qreal start = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    qreal extent = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && extent > 0) {
        extent -= 2 * std::numbers::pi;
    } else if (sweep && extent < 0) {
        extent += 2 * std::numbers::pi;
    }
    const int pieces = static_cast<int>(std::ceil(std::abs(extent) / (std::numbers::pi / 2) - 1e-9));
    const qreal step = extent / pieces;
    const qreal tangent = 4.0 / 3.0 * std::tan(step / 4.0);
    qreal theta = start;
    const auto point = [&](qreal t, qreal ox, qreal oy) {
        const qreal px = rx * (std::cos(t) + ox);
        const qreal py = ry * (std::sin(t) + oy);
        return QPointF(cos_phi * px - sin_phi * py + cx, sin_phi * px + cos_phi * py + cy);
    };
    for (int i = 0; i < pieces; ++i) {
        const qreal next = theta + step;
        const QPointF c1 = point(theta, -tangent * std::sin(theta), tangent * std::cos(theta));
        const QPointF c2 = point(next, tangent * std::sin(next), -tangent * std::cos(next));
        const QPointF end = i + 1 == pieces ? QPointF(x, y) : point(next, 0, 0);
        path.cubicTo(c1, c2, end);
        theta = next;
    }
}

class LineIconEngine final : public QIconEngine {
public:
    explicit LineIconEngine(LineIcon::Shape shape) : shape_(shape) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        LineIcon::paint(*painter, shape_, rect, color_for(mode));
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

    QIconEngine* clone() const override { return new LineIconEngine(shape_); }

private:
    static QColor color_for(QIcon::Mode mode) {
        switch (mode) {
        case QIcon::Active:
        case QIcon::Selected:
            return Theme::color(Theme::Color::kFg);
        case QIcon::Disabled:
            return Theme::color(Theme::Color::kFgFaint);
        case QIcon::Normal:
            break;
        }
        return Theme::color(Theme::Color::kFgDim);
    }

    LineIcon::Shape shape_;
};

}

QPainterPath LineIcon::path_data(const char* data) {
    QPainterPath path;
    const char* p = data;
    QPointF current;
    QPointF start;
    char command = 0;
    const auto skip = [&p] {
        while (*p == ' ' || *p == ',') {
            ++p;
        }
    };
    const auto number = [&p, &skip]() {
        skip();
        double value = 0;
        const char* last = p + std::strlen(p);
        const auto parsed = std::from_chars(p, last, value);
        p = parsed.ec == std::errc{} ? parsed.ptr : last;
        return value;
    };
    const auto flag = [&p, &skip]() {
        skip();
        const bool on = *p == '1';
        ++p;
        return on;
    };
    while (true) {
        skip();
        if (*p == '\0') {
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(*p)) != 0) {
            command = *p++;
            if (command == 'Z' || command == 'z') {
                path.closeSubpath();
                current = start;
                continue;
            }
        }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const QPointF base = relative ? current : QPointF();
        switch (command) {
        case 'M':
        case 'm': {
            const qreal x = number();
            const qreal y = number();
            current = base + QPointF(x, y);
            start = current;
            path.moveTo(current);
            command = relative ? 'l' : 'L';
            break;
        }
        case 'L':
        case 'l': {
            const qreal x = number();
            const qreal y = number();
            current = base + QPointF(x, y);
            path.lineTo(current);
            break;
        }
        case 'H':
        case 'h':
            current.setX(base.x() + number());
            path.lineTo(current);
            break;
        case 'V':
        case 'v':
            current.setY(base.y() + number());
            path.lineTo(current);
            break;
        case 'A':
        case 'a': {
            const qreal rx = number();
            const qreal ry = number();
            const qreal rotation = number();
            const bool large = flag();
            const bool sweep = flag();
            const qreal x = number();
            const qreal y = number();
            current = base + QPointF(x, y);
            svg_arc(path, rx, ry, rotation, large, sweep, current.x(), current.y());
            break;
        }
        default:
            return path;
        }
    }
    return path;
}

LineIcon::Built LineIcon::build(Shape shape) {
    Built out;
    const auto rect = [&out](qreal x, qreal y, qreal w, qreal h, qreal r) {
        out.stroked.addRoundedRect(QRectF(x, y, w, h), r, r);
    };
    const auto circle = [&out](qreal cx, qreal cy, qreal r) {
        out.stroked.addEllipse(QPointF(cx, cy), r, r);
    };
    const auto dot = [&out](qreal cx, qreal cy, qreal r) {
        out.filled.addEllipse(QPointF(cx, cy), r, r);
    };
    const auto lines = [&out](const char* data) { out.stroked.addPath(path_data(data)); };
    switch (shape) {
    case Shape::Overview:
        rect(3, 3, 6, 6, 1.5);
        rect(11, 3, 6, 6, 1.5);
        rect(3, 11, 6, 6, 1.5);
        rect(11, 11, 6, 6, 1.5);
        break;
    case Shape::Display:
        rect(2.5, 3.5, 15, 10.5, 2);
        lines("M10 14v2.5M6.5 16.5h7");
        break;
    case Shape::Graphics:
        rect(5.5, 5.5, 9, 9, 1.5);
        rect(8.5, 8.5, 3, 3, 0);
        lines("M8 2.5v3M12 2.5v3M8 14.5v3M12 14.5v3M2.5 8h3M2.5 12h3M14.5 8h3M14.5 12h3");
        break;
    case Shape::GpuStatus:
        rect(2.5, 4.5, 15, 11, 2);
        lines("M5.5 10h2.5l1.5-3 2 6 1.5-3h2");
        break;
    case Shape::Files:
        lines("M2.5 6.5v8a1.5 1.5 0 0 0 1.5 1.5h12a1.5 1.5 0 0 0 1.5-1.5V8a1.5 1.5 0 0 0-1.5-1.5H10"
              "L8.3 4.5H4A1.5 1.5 0 0 0 2.5 6Z");
        break;
    case Shape::Usb:
        rect(7.25, 2.5, 5.5, 4, 0.8);
        rect(5.5, 6.5, 9, 7, 2);
        lines("M10 13.5v4");
        break;
    case Shape::Console:
        rect(2.5, 3.5, 15, 13, 2);
        lines("M6 8l2.5 2.25L6 12.5M10.5 12.5H14");
        break;
    case Shape::Log:
        lines("M7.5 5.5h9M7.5 10h9M7.5 14.5h9");
        dot(4, 5.5, 1.15);
        dot(4, 10, 1.15);
        dot(4, 14.5, 1.15);
        break;
    case Shape::Maintenance:
        lines("M12.6 3.2a4 4 0 0 0-3.7 5.4l-5.6 5.6a1.6 1.6 0 0 0 2.3 2.3l5.6-5.6a4 4 0 0 0 5.4-3.7"
              "l-2.4 2.4-2.3-.6-.6-2.3Z");
        break;
    case Shape::Settings:
        lines("M2.5 5h2.6M8.9 5h8.6M2.5 10h8.6M14.9 10h2.6M2.5 15h4.6M10.9 15h6.6");
        circle(7, 5, 1.9);
        circle(13, 10, 1.9);
        circle(9, 15, 1.9);
        break;
    case Shape::Refresh:
        lines("M16 10a6 6 0 1 1-1.76-4.24M15.5 3v3.25h-3.25");
        break;
    case Shape::Plus:
        lines("M10 4.5v11M4.5 10h11");
        break;
    case Shape::Hid:
        rect(6, 2.5, 8, 15, 4);
        lines("M10 2.5v5M6 7.5h8");
        break;
    case Shape::Disk:
        rect(2.5, 5, 15, 10, 2);
        lines("M2.5 11h15");
        dot(14, 13, 0.6);
        break;
    case Shape::Count:
        break;
    }
    return out;
}

const LineIcon::Built& LineIcon::built(Shape shape) {
    static const std::array<Built, static_cast<std::size_t>(Shape::Count)> all = [] {
        std::array<Built, static_cast<std::size_t>(Shape::Count)> out;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = build(static_cast<Shape>(i));
        }
        return out;
    }();
    return all[static_cast<std::size_t>(shape)];
}

void LineIcon::paint(QPainter& painter, Shape shape, const QRectF& rect, const QColor& color) {
    const Built& icon = built(shape);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / kViewBox, rect.height() / kViewBox);
    painter.setPen(QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(icon.stroked);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(icon.filled);
    painter.restore();
}

QIcon LineIcon::icon(Shape shape) {
    return QIcon(new LineIconEngine(shape));
}

}
