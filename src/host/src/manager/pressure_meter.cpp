#include "lsw/host/manager/pressure_meter.hpp"

#include <algorithm>

#include <QPainter>
#include <QSizePolicy>
#include <QPainterPath>

namespace lsw::host::manager {

namespace {

// The bar's corner radius, matching the track it is drawn in.
constexpr double kRadius = 3.0;

// The bar's thickness, as QProgressBar#Meter was given in the
// stylesheet before this widget replaced it.
constexpr int kThickness = 7;

// How much of the fill colour the peak band keeps. Enough to read as
// the same colour at a glance and not enough to be mistaken for the
// live figure — the two are side by side and the eye has to tell which
// edge is which.
constexpr int kPeakAlpha = 125;

// A band that is a whole pixel wide however small the fraction is. A
// peak of a third of a per cent is still a peak, and a band rounded
// away says nothing was ever there.
double band_width(int percent, double full) {
    if (percent <= 0) {
        return 0.0;
    }
    return std::max(1.0, full * static_cast<double>(percent) / 100.0);
}

}  // namespace

PressureMeter::PressureMeter(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

QSize PressureMeter::sizeHint() const { return {0, kThickness}; }

QSize PressureMeter::minimumSizeHint() const { return {0, kThickness}; }

void PressureMeter::set_readings(int used_percent, int peak_percent) {
    const int used = std::clamp(used_percent, 0, 100);
    // The peak cannot be under the live figure. It can arrive that way
    // for a moment — the two are read from the region a field apart,
    // and a writer may land between them — and a peak band shorter than
    // the bar in front of it draws as a notch out of the fill.
    const int peak = std::clamp(std::max(peak_percent, used), 0, 100);
    if (used == used_ && peak == peak_) {
        return;
    }
    used_ = used;
    peak_ = peak;
    update();
}

QColor PressureMeter::track() const noexcept { return track_; }

void PressureMeter::set_track(const QColor& colour) {
    track_ = colour;
    update();
}

QColor PressureMeter::fill() const noexcept { return fill_; }

void PressureMeter::set_fill(const QColor& colour) {
    fill_ = colour;
    update();
}

void PressureMeter::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QRectF bounds(0.0, 0.0, static_cast<double>(width()),
                        static_cast<double>(height()));
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        return;
    }

    // Everything is clipped to the rounded track, so the two bands get
    // the bar's corners without having to be rounded themselves — a
    // rounded band inside a rounded track leaves a sliver of track
    // showing at the left edge.
    QPainterPath rounded;
    rounded.addRoundedRect(bounds, kRadius, kRadius);
    painter.setClipPath(rounded);

    painter.fillRect(bounds, track_);

    QColor peak_colour = fill_;
    peak_colour.setAlpha(kPeakAlpha);
    painter.fillRect(QRectF(bounds.left(), bounds.top(),
                            band_width(peak_, bounds.width()), bounds.height()),
                     peak_colour);

    painter.fillRect(QRectF(bounds.left(), bounds.top(),
                            band_width(used_, bounds.width()), bounds.height()),
                     fill_);
}

}  // namespace lsw::host::manager
