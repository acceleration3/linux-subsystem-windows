#include "lsw/host/ui/usage_graph.hpp"

#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace lsw::host::ui {

namespace {

constexpr int kHeight = 120;
constexpr int kMinWidth = 160;
constexpr int kLabelPx = 10;
constexpr int kInset = 6;
constexpr int kAreaAlpha = 150;

QColor with_alpha(const char* hex, int alpha) {
    QColor color = Theme::color(hex);
    color.setAlpha(alpha);
    return color;
}

QString span_text(qint64 span_ms) {
    const qint64 minutes = span_ms / 60000;
    return minutes >= 1 ? QStringLiteral("%1 min").arg(minutes)
                        : QStringLiteral("%1 s").arg(span_ms / 1000);
}

}

UsageGraph::UsageGraph(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void UsageGraph::set_series(const QVector<Point>& points, std::optional<quint64> ceiling,
                            qint64 now_ms, qint64 span_ms) {
    points_ = points;
    ceiling_ = ceiling;
    now_ms_ = now_ms;
    span_ms_ = std::max<qint64>(span_ms, 1);
    update();
}

QSize UsageGraph::sizeHint() const {
    return {kMinWidth * 2, kHeight};
}

QSize UsageGraph::minimumSizeHint() const {
    return {kMinWidth, kHeight};
}

void UsageGraph::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(Theme::color(Theme::Color::kLine), 1));
    painter.setBrush(Theme::color(Theme::Color::kBase));
    painter.drawRoundedRect(frame, 4, 4);

    const QFont label_font = Theme::mono_font(kLabelPx);
    const QFontMetrics metrics(label_font);
    const QRectF plot = frame.adjusted(kInset, kInset + metrics.height(), -kInset,
                                       -(kInset + metrics.height()));
    painter.setFont(label_font);
    painter.setPen(Theme::color(Theme::Color::kFgFaint));
    painter.drawText(QRectF(frame.left() + kInset, frame.bottom() - kInset - metrics.height(),
                            frame.width() / 2, metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, span_text(span_ms_));
    if (points_.isEmpty()) {
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("-"));
        return;
    }

    quint64 top = ceiling_.value_or(0);
    for (const Point& point : points_) {
        top = std::max(top, point.upper);
    }
    if (top == 0) {
        top = 1;
    }
    const qint64 from = now_ms_ - span_ms_;
    const auto x_at = [&](qint64 at) {
        return plot.left() + plot.width() * static_cast<double>(at - from) / static_cast<double>(span_ms_);
    };
    const auto y_at = [&](quint64 value) {
        return plot.bottom() - plot.height() * static_cast<double>(value) / static_cast<double>(top);
    };
    painter.drawText(QRectF(frame.left() + kInset, frame.top() + kInset / 2.0, frame.width() / 2,
                            metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, Format::bytes(top));

    painter.save();
    painter.setClipRect(plot);
    const auto fill = [&](bool upper_band, const QColor& color) {
        QPainterPath path;
        path.moveTo(x_at(points_.first().at_ms), y_at(upper_band ? points_.first().upper : points_.first().lower));
        for (const Point& point : points_) {
            path.lineTo(x_at(point.at_ms), y_at(upper_band ? point.upper : point.lower));
        }
        for (qsizetype i = points_.size() - 1; i >= 0; --i) {
            path.lineTo(x_at(points_.at(i).at_ms), y_at(upper_band ? points_.at(i).lower : 0));
        }
        path.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(path);
    };
    fill(false, with_alpha(Theme::Color::kInfo, kAreaAlpha));
    fill(true, with_alpha(Theme::Color::kAmber, kAreaAlpha));
    painter.restore();

    if (ceiling_) {
        QPen dashed(Theme::color(Theme::Color::kFgDim), 1, Qt::DashLine);
        painter.setPen(dashed);
        const qreal y = y_at(*ceiling_);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
}

}
