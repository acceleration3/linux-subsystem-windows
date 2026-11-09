#include "render/common/capture_dump.hpp"

#include "render/common/png.hpp"
#include "render/common/ref_store.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace lsw::render {

bool CaptureDump::enabled() {
    const char* env = std::getenv("LSW_RENDER_DUMP");
    return env != nullptr && *env != '\0' && *env != '0';
}

std::string CaptureDump::hash(const Image& image) {
    std::uint64_t value = 1469598103934665603ull;
    const auto mix = [&value](std::uint8_t byte) { value = (value ^ byte) * 1099511628211ull; };
    const std::uint32_t head[3] = {image.width(), image.height(), static_cast<std::uint32_t>(image.kind())};
    for (std::uint32_t word : head) {
        for (int i = 0; i < 4; ++i) {
            mix(static_cast<std::uint8_t>(word >> (8 * i)));
        }
    }
    for (std::uint8_t byte : image.bytes()) {
        mix(byte);
    }
    char out[24];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(value));
    return out;
}

Image CaptureDump::preview(const Image& image) {
    const PixelKind kind = image.kind();
    if (kind == PixelKind::Depth32Float || kind == PixelKind::R32Float) {
        double low = INFINITY;
        double high = -INFINITY;
        for (std::uint32_t y = 0; y < image.height(); ++y) {
            for (std::uint32_t x = 0; x < image.width(); ++x) {
                const double v = image.channel(x, y, 0);
                if (std::isfinite(v)) {
                    low = std::min(low, v);
                    high = std::max(high, v);
                }
            }
        }
        Image out(image.width(), image.height(), PixelKind::Rgba8Unorm);
        const double span = high > low ? high - low : 1.0;
        for (std::uint32_t y = 0; y < image.height(); ++y) {
            for (std::uint32_t x = 0; x < image.width(); ++x) {
                const double v = image.channel(x, y, 0);
                const std::uint32_t g = std::isfinite(v) ? static_cast<std::uint32_t>(std::clamp((v - low) / span, 0.0, 1.0) * 255.0 + 0.5)
                                                         : 255u;
                out.set_rgba8(x, y, g | (g << 8) | (g << 16) | 0xff000000u);
            }
        }
        return out;
    }
    if (PixelInfo::is_integer(kind)) {
        Image out(image.width(), image.height(), PixelKind::Rgba8Unorm);
        const unsigned channels = PixelInfo::channels(kind);
        for (std::uint32_t y = 0; y < image.height(); ++y) {
            for (std::uint32_t x = 0; x < image.width(); ++x) {
                std::uint64_t h = 1469598103934665603ull;
                bool zero = true;
                for (unsigned c = 0; c < channels; ++c) {
                    const auto v = static_cast<std::uint64_t>(image.channel(x, y, c));
                    zero = zero && v == 0;
                    h = (h ^ v) * 1099511628211ull;
                    h ^= h >> 29;
                }
                std::uint32_t rgba = 0xff000000u;
                if (!zero) {
                    h *= 0x9e3779b97f4a7c15ull;
                    const auto channel = [&h](int shift) { return 64u + static_cast<std::uint32_t>((h >> shift) % 192u); };
                    rgba |= channel(20) | (channel(32) << 8) | (channel(44) << 16);
                }
                out.set_rgba8(x, y, rgba);
            }
        }
        return out;
    }
    return image;
}

std::string CaptureDump::stats(const Image& image, const Image* previous) {
    const std::size_t pixel = PixelInfo::bytes(image.kind());
    const std::size_t count = image.bytes().size() / pixel;
    std::unordered_map<std::uint64_t, std::pair<std::size_t, std::size_t>> seen;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t b = 0; b < pixel; ++b) {
            h = (h ^ image.bytes()[i * pixel + b]) * 1099511628211ull;
        }
        auto found = seen.emplace(h, std::make_pair(i, std::size_t{0})).first;
        ++found->second.second;
    }
    std::size_t dominant = 0;
    std::size_t dominant_count = 0;
    for (const auto& entry : seen) {
        if (entry.second.second > dominant_count || (entry.second.second == dominant_count && entry.second.first < dominant)) {
            dominant_count = entry.second.second;
            dominant = entry.second.first;
        }
    }
    std::string hex;
    char text[4];
    for (std::size_t b = 0; b < pixel; ++b) {
        std::snprintf(text, sizeof text, "%02x", image.bytes()[dominant * pixel + b]);
        hex += text;
    }
    const unsigned channels = PixelInfo::channels(image.kind());
    double deviation = 0.0;
    for (unsigned c = 0; c < channels; ++c) {
        double sum = 0.0;
        double square = 0.0;
        std::size_t n = 0;
        for (std::uint32_t y = 0; y < image.height(); ++y) {
            for (std::uint32_t x = 0; x < image.width(); ++x) {
                const double v = image.channel(x, y, c);
                if (std::isfinite(v)) {
                    sum += v;
                    square += v * v;
                    ++n;
                }
            }
        }
        if (n > 0) {
            const double mean = sum / static_cast<double>(n);
            deviation += std::sqrt(std::max(0.0, square / static_cast<double>(n) - mean * mean));
        }
    }
    std::string out = "{\"distinct\":" + std::to_string(seen.size()) + ",\"dominant\":\"" + hex + "\",\"dominant_fraction\":" +
                      number(static_cast<double>(dominant_count) / static_cast<double>(std::max<std::size_t>(count, 1))) +
                      ",\"stddev\":" + number(channels > 0 ? deviation / channels : 0.0);
    if (previous != nullptr && previous->kind() == image.kind() && previous->bytes().size() == image.bytes().size()) {
        std::size_t changed = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (std::memcmp(&image.bytes()[i * pixel], &previous->bytes()[i * pixel], pixel) != 0) {
                ++changed;
            }
        }
        out += ",\"changed_from_previous\":" + number(static_cast<double>(changed) / static_cast<double>(std::max<std::size_t>(count, 1)));
    }
    return out + "}";
}

std::string CaptureDump::file_label(unsigned frame, const std::string& label) {
    char text[16];
    std::snprintf(text, sizeof text, "f%03u", frame);
    return label.empty() ? std::string(text) : std::string(text) + "_" + label;
}

std::string CaptureDump::escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::string CaptureDump::number(double value) {
    if (std::isnan(value)) {
        return "null";
    }
    if (std::isinf(value)) {
        return value > 0 ? "1e999" : "-1e999";
    }
    char out[40];
    std::snprintf(out, sizeof out, "%.9g", value);
    return out;
}

std::vector<std::filesystem::path> CaptureDump::write(const std::filesystem::path& dir, const std::string& id, const CaseSpec& spec,
                                                      const std::vector<CaptureRecord>& records, const std::string& side,
                                                      const std::string& outcome, bool write_images) {
    std::vector<std::filesystem::path> written;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    std::vector<unsigned> frames;
    for (const CaptureRecord& r : records) {
        if (std::find(frames.begin(), frames.end(), r.frame) == frames.end()) {
            frames.push_back(r.frame);
        }
    }
    std::string json = "{\"id\":\"" + escape(id) + "\",\"side\":\"" + side + "\",\"outcome\":\"" + escape(outcome) +
                       "\",\"frames\":" + std::to_string(spec.frame_count()) + ",\"uniform_ok\":[";
    bool first_label = true;
    for (const std::string& label : spec.uniform_labels()) {
        json += (first_label ? "\"" : ",\"") + escape(label) + "\"";
        first_label = false;
    }
    json += "],\"audit_ok\":[";
    for (std::size_t i = 0; i < spec.audit_reasons().size(); ++i) {
        json += (i ? ",\"" : "\"") + escape(spec.audit_reasons()[i]) + "\"";
    }
    json += "],\"captured\":[";
    for (std::size_t i = 0; i < frames.size(); ++i) {
        json += (i ? "," : "") + std::to_string(frames[i]);
    }
    json += "],\"captures\":[";
    bool first = true;
    for (const CaptureRecord& r : records) {
        const Image* main = r.actual != nullptr ? r.actual : r.oracle;
        if (main == nullptr) {
            continue;
        }
        json += first ? "" : ",";
        first = false;
        json += "{\"frame\":" + std::to_string(r.frame) + ",\"label\":\"" + escape(r.label) + "\",\"width\":" + std::to_string(main->width()) +
                ",\"height\":" + std::to_string(main->height()) + ",\"kind\":\"" + PixelInfo::name(main->kind()) + "\"";
        if (r.actual != nullptr) {
            json += ",\"actual_hash\":\"" + hash(*r.actual) + "\"";
        }
        const CaptureRecord* before = nullptr;
        for (const CaptureRecord& other : records) {
            if (&other == &r) {
                break;
            }
            if (other.label == r.label && other.frame < r.frame) {
                before = &other;
            }
        }
        if (r.actual != nullptr) {
            json += ",\"actual_stats\":" + stats(*r.actual, before != nullptr ? before->actual : nullptr);
        }
        if (r.oracle != nullptr) {
            json += ",\"oracle_hash\":\"" + hash(*r.oracle) + "\",\"oracle_stats\":" + stats(*r.oracle, before != nullptr ? before->oracle : nullptr);
        }
        const Tolerance& t = r.tolerance;
        json += ",\"tolerance\":{\"name\":\"" + escape(t.name) + "\",\"byte_delta\":" + std::to_string(t.byte_delta) +
                ",\"absolute\":" + number(t.absolute) + ",\"relative\":" + number(t.relative) +
                ",\"outlier_fraction\":" + number(t.outlier_fraction) + ",\"edge_radius\":" + std::to_string(t.edge_radius) +
                ",\"forgive_fraction\":" + number(t.forgive_fraction) + ",\"min_psnr\":" + number(t.min_psnr) +
                ",\"outlier_max_delta\":" + number(t.outlier_max_delta) + ",\"max_outliers\":" + std::to_string(t.max_outliers) + "}";
        if (r.judged) {
            json += ",\"psnr\":" + number(r.result.psnr) + ",\"bad\":" + std::to_string(r.result.bad) +
                    ",\"max_delta\":" + number(r.result.max_delta) + ",\"forgiven\":" + std::to_string(r.result.forgiven) +
                    ",\"total\":" + std::to_string(r.result.total) + ",\"pass\":" + (r.result.pass ? "true" : "false");
        }
        json += "}";
        if (write_images) {
            const std::string stem = file_label(r.frame, r.label);
            const struct {
                const char* suffix;
                const Image* image;
            } files[] = {{".actual.png", r.actual}, {".oracle.png", r.oracle}};
            for (const auto& file : files) {
                if (file.image == nullptr || file.image->empty()) {
                    continue;
                }
                const std::filesystem::path path = dir / (stem + file.suffix);
                if (Png::write(path, preview(*file.image))) {
                    written.push_back(path);
                }
                std::string ignored_error;
                const std::filesystem::path raw = dir / (stem + std::string(file.suffix).replace(std::string(file.suffix).size() - 4, 4, ".rref"));
                if (RefStore::write_image(raw, *file.image, ignored_error)) {
                    written.push_back(raw);
                }
            }
        }
    }
    json += "]}\n";
    const std::filesystem::path path = dir / "captures.json";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << json;
    if (file) {
        written.push_back(path);
    }
    return written;
}

}  // namespace lsw::render
