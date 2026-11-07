#include "sanity/interop/kit.hpp"

#include "support/api_policy.hpp"

#include <algorithm>
#include <cctype>
#include <thread>

namespace lsw::suites::sanity::interop {

std::string Kit::missing(std::initializer_list<const char*> apis) {
    std::string out;
    for (const char* api : apis) {
        if (ApiPolicy::is_skipped(api)) {
            out += (out.empty() ? "" : ", ") + std::string(api);
        }
    }
    return out.empty() ? out : "this row needs " + std::string(apis.size() > 1 ? "all of its APIs" : "its API") + " and " + out + " is excluded by LSW_SUITES_SKIP_APIS";
}

void Kit::keep_image(Context& context, const std::string& name, const Bitmap& image) {
    ArtifactWriter writer(context);
    writer.image(sanitize(name), image.pixels.data(), static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height),
                 static_cast<std::uint32_t>(image.width) * 4u);
}

void Kit::keep_pixels(Context& context, const std::string& name, const std::vector<std::uint32_t>& pixels, int width, int height) {
    ArtifactWriter writer(context);
    writer.image(sanitize(name), pixels.data(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), static_cast<std::uint32_t>(width) * 4u);
}

std::string Kit::sanitize(std::string text) {
    for (char& c : text) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return text;
}

bool Kit::judge(Context& context, Problems& problems, const std::string& label, const Bitmap& got, int origin_x, int origin_y, int width,
                int height, const std::vector<std::uint32_t>& expected, int tolerance) {
    std::string where;
    if (ScreenCapture::matches_region(got, origin_x, origin_y, width, height, expected, tolerance, where)) {
        return true;
    }
    problems.add(label + ": " + where);
    keep_pixels(context, label + ".expected", expected, width, height);
    Bitmap region;
    region.width = width;
    region.height = height;
    region.pixels.resize(expected.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            region.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                got.valid(origin_x + x, origin_y + y) ? got.at(origin_x + x, origin_y + y) : 0u;
        }
    }
    keep_image(context, label + ".actual", region);
    return false;
}

bool Kit::settle_screen(Context& context, const std::string& label, const RECT& area, const std::vector<std::uint32_t>& expected, int tolerance,
                        int timeout_ms, std::string& detail) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    Bitmap last;
    std::string error;
    const int width = static_cast<int>(area.right - area.left);
    const int height = static_cast<int>(area.bottom - area.top);
    do {
        if (!ScreenCapture::screen(area, false, last, error)) {
            detail = error;
            return false;
        }
        if (ScreenCapture::matches(last, expected, tolerance, detail)) {
            return true;
        }
        ::Sleep(15);
    } while (std::chrono::steady_clock::now() < deadline);
    keep_pixels(context, label + ".expected", expected, width, height);
    keep_image(context, label + ".actual", last);
    return false;
}

std::vector<std::uint32_t> Kit::uniform(int width, int height, std::uint32_t xrgb) {
    return std::vector<std::uint32_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), xrgb);
}

void Kit::paint(std::vector<std::uint32_t>& image, int width, const RECT& rect, std::uint32_t xrgb) {
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            image[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = xrgb;
        }
    }
}

std::vector<std::uint32_t> Kit::blend(const std::vector<std::uint32_t>& below, const std::vector<std::uint32_t>& premultiplied,
                                      const std::vector<std::uint8_t>& alpha) {
    std::vector<std::uint32_t> out(below.size());
    for (std::size_t i = 0; i < below.size(); ++i) {
        std::uint32_t value = 0;
        for (int shift = 16; shift >= 0; shift -= 8) {
            const int src = static_cast<int>((premultiplied[i] >> shift) & 255u);
            const int dst = static_cast<int>((below[i] >> shift) & 255u);
            const int mixed = src + ((255 - alpha[i]) * dst + 127) / 255;
            value |= static_cast<std::uint32_t>(std::min(mixed, 255)) << shift;
        }
        out[i] = value;
    }
    return out;
}

HANDLE Kit::adopt_handle(const std::string& process_id, const std::string& value) {
    if (process_id.empty() || value.empty()) {
        return nullptr;
    }
    HANDLE parent = ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, static_cast<DWORD>(std::stoul(process_id)));
    if (parent == nullptr) {
        return nullptr;
    }
    HANDLE local = nullptr;
    const HANDLE remote = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(value)));
    if (::DuplicateHandle(parent, remote, ::GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE) {
        local = nullptr;
    }
    ::CloseHandle(parent);
    return local;
}

std::vector<std::uint8_t> Kit::frame_bytes(std::uint64_t base, int frame, int width, int height) {
    std::vector<std::uint8_t> data(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    Pattern::fill_pixels(base + static_cast<std::uint64_t>(frame) * 7919u, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                         static_cast<std::uint32_t>(width) * 4u, data.data());
    return data;
}

}  // namespace lsw::suites::sanity::interop
