#include "render/common/diff.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace lsw::render {

namespace {

std::uint64_t drawn_pixels(const Image& image, unsigned channels) {
    std::map<std::array<double, 4>, std::uint64_t> histogram;
    std::uint64_t dominant = 0;
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            std::array<double, 4> key{};
            for (unsigned c = 0; c < channels && c < 4; ++c) {
                key[c] = image.channel(x, y, c);
            }
            dominant = std::max(dominant, ++histogram[key]);
        }
    }
    return static_cast<std::uint64_t>(image.width()) * image.height() - dominant;
}

constexpr std::uint64_t kForgiveFloor = 2;

bool channel_close(ChannelClass klass, double a, double b, const Tolerance& tolerance, double& units) {
    if (std::isnan(a) || std::isnan(b)) {
        const bool both = std::isnan(a) && std::isnan(b);
        units = both ? 0.0 : 255.0;
        return both;
    }
    if (a == b) {
        units = 0.0;
        return true;
    }
    if (std::isinf(a) || std::isinf(b)) {
        units = 1e9;
        return false;
    }
    const double delta = std::fabs(a - b);
    switch (klass) {
        case ChannelClass::Float:
            units = delta * 255.0;
            return delta <= static_cast<double>(tolerance.absolute) + static_cast<double>(tolerance.relative) * std::max(std::fabs(a), std::fabs(b));
        case ChannelClass::Integer:
            units = delta;
            return false;
        case ChannelClass::Unorm16:
            units = delta * 255.0 / 65535.0;
            return units <= static_cast<double>(tolerance.byte_delta);
        case ChannelClass::Unorm8:
            units = delta;
            return delta <= static_cast<double>(tolerance.byte_delta);
    }
    return false;
}

}  // namespace

bool ImageDiff::close(const Image& a, std::uint32_t ax, std::uint32_t ay, const Image& b, std::uint32_t bx,
                      std::uint32_t by, const Tolerance& tolerance, double& worst) {
    bool ok = true;
    worst = 0.0;
    const ChannelClass klass = PixelInfo::channel_class(a.kind());
    for (unsigned c = 0; c < PixelInfo::channels(a.kind()); ++c) {
        double units = 0.0;
        if (!channel_close(klass, a.channel(ax, ay, c), b.channel(bx, by, c), tolerance, units)) {
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
    if (width == 0 || height == 0) {
        result.detail = "image is empty";
        return result;
    }
    const PixelKind kind = expected.kind();
    const ChannelClass klass = PixelInfo::channel_class(kind);
    const bool integer = klass == ChannelClass::Integer;
    const unsigned channels = PixelInfo::channels(kind);
    result.total = static_cast<std::uint64_t>(width) * height;
    std::vector<unsigned char> state(result.total, 0);
    std::vector<float> worst_of(result.total, 0.0f);
    double squared = 0.0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            double worst = 0.0;
            const bool ok = close(expected, x, y, actual, x, y, tolerance, worst);
            const std::size_t at = static_cast<std::size_t>(y) * width + x;
            worst_of[at] = static_cast<float>(worst);
            state[at] = ok ? 0 : 1;
            result.max_delta = std::max(result.max_delta, worst);
            for (unsigned c = 0; c < channels; ++c) {
                double d = expected.channel(x, y, c) - actual.channel(x, y, c);
                if (std::isnan(d)) {
                    d = 1.0;
                }
                switch (klass) {
                    case ChannelClass::Float: d = std::clamp(d, -1.0, 1.0); break;
                    case ChannelClass::Unorm16: d /= 65535.0; break;
                    default: d = std::clamp(d / 255.0, -1.0, 1.0); break;
                }
                squared += d * d;
            }
        }
    }
    if (tolerance.edge_radius > 0 && (!integer || tolerance.integer_edges)) {
        const int radius = tolerance.edge_radius;
        const std::uint64_t cap = std::max(kForgiveFloor, static_cast<std::uint64_t>(tolerance.forgive_fraction * static_cast<double>(drawn_pixels(expected, channels))));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * width + x;
                if (state[at] != 1 || result.forgiven >= cap) {
                    continue;
                }
                bool expected_in_actual = false;
                bool actual_in_expected = false;
                bool edge = false;
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int nx = static_cast<int>(x) + dx;
                        const int ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height)) {
                            continue;
                        }
                        const std::uint32_t ux = static_cast<std::uint32_t>(nx);
                        const std::uint32_t uy = static_cast<std::uint32_t>(ny);
                        double ignored = 0.0;
                        if (close(expected, x, y, actual, ux, uy, tolerance, ignored)) {
                            expected_in_actual = true;
                        }
                        if (close(expected, ux, uy, actual, x, y, tolerance, ignored)) {
                            actual_in_expected = true;
                        }
                        if (!close(expected, x, y, expected, ux, uy, tolerance, ignored)) {
                            edge = true;
                        }
                    }
                }
                if (expected_in_actual && actual_in_expected && edge) {
                    state[at] = 2;
                    ++result.forgiven;
                }
            }
        }
    }
    std::uint64_t hard = 0;
    for (std::size_t i = 0; i < state.size(); ++i) {
        if (state[i] == 1) {
            ++result.bad;
            if (tolerance.outlier_max_delta > 0.0 && static_cast<double>(worst_of[i]) > tolerance.outlier_max_delta) {
                ++hard;
            }
        }
    }
    const double mse = squared / (static_cast<double>(result.total) * channels);
    result.psnr = mse <= 0.0 ? 999.0 : 10.0 * std::log10(1.0 / mse);
    double allowed = tolerance.outlier_fraction * static_cast<double>(result.total);
    if (tolerance.max_outliers > 0) {
        allowed = std::min(allowed, static_cast<double>(tolerance.max_outliers));
    }
    const bool psnr_ok = integer || result.psnr >= tolerance.min_psnr;
    result.pass = static_cast<double>(result.bad) <= allowed && hard == 0 && psnr_ok;
    if (diff_out != nullptr) {
        Image diff(width, height, PixelKind::Rgba8Unorm);
        const Image base = expected.to_rgba8();
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * width + x;
                if (state[at] == 0) {
                    const std::uint32_t p = base.rgba8(x, y);
                    const std::uint32_t gray = ((p & 0xffu) + ((p >> 8) & 0xffu) + ((p >> 16) & 0xffu)) / 3u / 3u;
                    diff.set_rgba8(x, y, gray | (gray << 8) | (gray << 16) | 0xff000000u);
                } else if (state[at] == 2) {
                    diff.set_rgba8(x, y, 0xff00c0c0u);
                } else {
                    const std::uint32_t amp = static_cast<std::uint32_t>(std::min(255.0, 96.0 + static_cast<double>(worst_of[at]) * 4.0));
                    diff.set_rgba8(x, y, amp | 0xff000000u);
                }
            }
        }
        *diff_out = std::move(diff);
    }
    char text[280];
    if (result.pass) {
        std::snprintf(text, sizeof text, "psnr %.2f dB, max delta %.2f, %llu forgiven under %s", result.psnr, result.max_delta,
                      static_cast<unsigned long long>(result.forgiven), tolerance.name.c_str());
    } else if (result.bad != 0) {
        std::snprintf(text, sizeof text, "%llu of %llu pixels differ beyond %s (%llu forgiven, %llu large; max delta %.2f, psnr %.2f dB)",
                      static_cast<unsigned long long>(result.bad), static_cast<unsigned long long>(result.total), tolerance.name.c_str(),
                      static_cast<unsigned long long>(result.forgiven), static_cast<unsigned long long>(hard), result.max_delta, result.psnr);
    } else {
        std::snprintf(text, sizeof text, "psnr %.2f dB below %.2f required by %s", result.psnr, tolerance.min_psnr, tolerance.name.c_str());
    }
    result.detail = text;
    return result;
}

}  // namespace lsw::render
