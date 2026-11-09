#include "render/common/diff.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lsw::render {

namespace {

bool channel_close(PixelKind kind, double a, double b, const Tolerance& tolerance, double& delta_units) {
    if (std::isnan(a) || std::isnan(b)) {
        delta_units = (std::isnan(a) && std::isnan(b)) ? 0.0 : 255.0;
        return std::isnan(a) && std::isnan(b);
    }
    const double delta = std::fabs(a - b);
    if (PixelInfo::is_float(kind)) {
        delta_units = delta * 255.0;
        const double allowed = tolerance.absolute + tolerance.relative * std::max(std::fabs(a), std::fabs(b)) +
                               static_cast<double>(tolerance.byte_delta) / 255.0;
        return delta <= allowed;
    }
    delta_units = delta;
    return delta <= static_cast<double>(tolerance.byte_delta);
}

}  // namespace

bool ImageDiff::close(const Image& a, std::uint32_t ax, std::uint32_t ay, const Image& b, std::uint32_t bx,
                      std::uint32_t by, const Tolerance& tolerance, double& worst) {
    bool ok = true;
    worst = 0.0;
    for (unsigned c = 0; c < PixelInfo::channels(a.kind()); ++c) {
        double units = 0.0;
        if (!channel_close(a.kind(), a.channel(ax, ay, c), b.channel(bx, by, c), tolerance, units)) {
            ok = false;
        }
        worst = std::max(worst, units);
    }
    return ok;
}

DiffResult ImageDiff::compare(const Image& expected, const Image& actual, const Tolerance& tolerance, Image* diff_out) {
    DiffResult result;
    if (expected.width() != actual.width() || expected.height() != actual.height() || expected.kind() != actual.kind()) {
        char text[200];
        std::snprintf(text, sizeof text, "image shape differs: expected %ux%u %s, actual %ux%u %s", expected.width(),
                      expected.height(), PixelInfo::name(expected.kind()), actual.width(), actual.height(),
                      PixelInfo::name(actual.kind()));
        result.detail = text;
        return result;
    }
    const std::uint32_t width = expected.width();
    const std::uint32_t height = expected.height();
    const unsigned channels = PixelInfo::channels(expected.kind());
    result.total = static_cast<std::uint64_t>(width) * height;
    Image diff;
    if (diff_out != nullptr) {
        diff = Image(width, height, PixelKind::Rgba8Unorm);
    }
    double squared = 0.0;
    std::uint32_t first_x = 0;
    std::uint32_t first_y = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            double worst = 0.0;
            bool ok = close(expected, x, y, actual, x, y, tolerance, worst);
            result.max_delta = std::max(result.max_delta, worst);
            for (unsigned c = 0; c < channels; ++c) {
                double d = expected.channel(x, y, c) - actual.channel(x, y, c);
                if (std::isnan(d)) {
                    d = 1.0;
                }
                d = PixelInfo::is_float(expected.kind()) ? std::clamp(d, -1.0, 1.0) : std::clamp(d / 255.0, -1.0, 1.0);
                squared += d * d;
            }
            if (!ok && tolerance.edge_radius > 0) {
                bool forgiven = false;
                const int radius = tolerance.edge_radius;
                for (int dy = -radius; dy <= radius && !forgiven; ++dy) {
                    for (int dx = -radius; dx <= radius && !forgiven; ++dx) {
                        const int nx = static_cast<int>(x) + dx;
                        const int ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height)) {
                            continue;
                        }
                        double ignored = 0.0;
                        if (close(expected, static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny), actual, x, y,
                                  tolerance, ignored) ||
                            close(expected, x, y, actual, static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny),
                                  tolerance, ignored)) {
                            forgiven = true;
                        }
                    }
                }
                ok = forgiven;
            }
            if (!ok) {
                if (result.bad == 0) {
                    first_x = x;
                    first_y = y;
                }
                ++result.bad;
            }
        }
    }
    const double mse = squared / (static_cast<double>(result.total) * channels);
    result.psnr = mse <= 0.0 ? 999.0 : 10.0 * std::log10(1.0 / mse);
    const double allowed = tolerance.outlier_fraction * static_cast<double>(result.total);
    result.pass = static_cast<double>(result.bad) <= allowed && result.psnr >= tolerance.min_psnr;
    if (diff_out != nullptr) {
        const Image base = expected.to_rgba8();
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                double worst = 0.0;
                const bool ok = close(expected, x, y, actual, x, y, tolerance, worst);
                if (ok) {
                    const std::uint32_t p = base.rgba8(x, y);
                    const std::uint32_t gray = ((p & 0xffu) + ((p >> 8) & 0xffu) + ((p >> 16) & 0xffu)) / 3u / 3u;
                    diff.set_rgba8(x, y, gray | (gray << 8) | (gray << 16) | 0xff000000u);
                } else {
                    const std::uint32_t amp = static_cast<std::uint32_t>(std::min(255.0, 96.0 + worst * 4.0));
                    diff.set_rgba8(x, y, amp | 0xff000000u);
                }
            }
        }
        *diff_out = std::move(diff);
    }
    char text[240];
    if (result.bad != 0) {
        std::snprintf(text, sizeof text, "%llu of %llu pixels differ beyond %s (max delta %.2f, psnr %.2f dB, first at %u,%u)",
                      static_cast<unsigned long long>(result.bad), static_cast<unsigned long long>(result.total),
                      tolerance.name.c_str(), result.max_delta, result.psnr, first_x, first_y);
    } else {
        std::snprintf(text, sizeof text, "psnr %.2f dB, max delta %.2f under %s", result.psnr, result.max_delta,
                      tolerance.name.c_str());
    }
    result.detail = text;
    if (!result.pass && result.bad == 0) {
        std::snprintf(text, sizeof text, "psnr %.2f dB below %.2f required by %s", result.psnr, tolerance.min_psnr,
                      tolerance.name.c_str());
        result.detail = text;
    }
    return result;
}

}  // namespace lsw::render
