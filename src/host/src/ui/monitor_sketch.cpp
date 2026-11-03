#include "lsw/host/ui/monitor_sketch.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QPainter>

#include <algorithm>

namespace lsw::host::ui {

namespace {
constexpr int kScreenWidth = 210;
constexpr int kStandHeight = 12;
constexpr int kStandWidth = 2;
constexpr int kBaseWidth = 64;
constexpr int kBaseHeight = 2;
constexpr qreal kBorder = 1.5;
constexpr qreal kRadius = 6.0;
constexpr int kTitlePx = 14;
constexpr int kDetailPx = 11;
constexpr int kMinHeight = 60;
}

MonitorSketch::MonitorSketch(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void MonitorSketch::show_mode(int width, int height, const QString& title, const QString& detail) {
    aspect_width_ = width > 0 && height > 0 ? width : 16;
    aspect_height_ = width > 0 && height > 0 ? height : 9;
    title_ = title;
    detail_ = detail;
    updateGeometry();
    update();
}

int MonitorSketch::screen_height() const {
    const int height = kScreenWidth * aspect_height_ / aspect_width_;
    return std::max(height, kMinHeight);
}

QSize MonitorSketch::sizeHint() const {
    return {kScreenWidth, screen_height() + kStandHeight + kBaseHeight};
}

void MonitorSketch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor frame = Theme::color("#39415a");
    const QRectF screen(kBorder / 2, kBorder / 2, kScreenWidth - kBorder, screen_height() - kBorder);
    painter.setPen(QPen(frame, kBorder));
    painter.setBrush(Theme::color("#121722"));
    painter.drawRoundedRect(screen, kRadius, kRadius);

    QFont title = Theme::mono_font(kTitlePx);
    title.setWeight(QFont::Medium);
    const QFontMetrics title_metrics(title);
    const QFont detail = Theme::mono_font(kDetailPx);
    const QFontMetrics detail_metrics(detail);
    const int gap = 3;
    const int block = title_metrics.height() + gap + detail_metrics.height();
    const qreal top = screen.top() + (screen.height() - block) / 2.0;
    painter.setFont(title);
    painter.setPen(Theme::color(Theme::Color::kFg));
    painter.drawText(QRectF(screen.left(), top, screen.width(), title_metrics.height()),
                     Qt::AlignHCenter | Qt::AlignVCenter, title_);
    painter.setFont(detail);
    painter.setPen(Theme::color(Theme::Color::kFgFaint));
    painter.drawText(QRectF(screen.left(), top + title_metrics.height() + gap, screen.width(),
                            detail_metrics.height()),
                     Qt::AlignHCenter | Qt::AlignVCenter, detail_);

    painter.setPen(Qt::NoPen);
    painter.setBrush(frame);
    const int mid = kScreenWidth / 2;
    painter.drawRect(QRect(mid - kStandWidth / 2, screen_height(), kStandWidth, kStandHeight));
    painter.drawRoundedRect(QRectF(mid - kBaseWidth / 2.0, screen_height() + kStandHeight,
                                   kBaseWidth, kBaseHeight),
                            1, 1);
}

}
