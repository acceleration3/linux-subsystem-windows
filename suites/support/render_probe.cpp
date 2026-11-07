#include "support/render_probe.hpp"

#include "support/d3d11_tools.hpp"
#include "support/target_adapter.hpp"

#include <cstdlib>

namespace lsw::suites {

namespace {

constexpr UINT kSide = 256;

}  // namespace

bool RenderProbe::open(std::string& error) {
    AvAdapter adapter;
    if (!TargetAdapter::pick(adapter, error) || !device_.create(adapter.luid, error)) {
        return false;
    }
    target_ = D3d11Tools::target(device_, kSide, kSide);
    view_ = D3d11Tools::rtv(device_, target_.get());
    if (!target_ || !view_) {
        error = "creating the probe render target failed";
        return false;
    }
    return true;
}

bool RenderProbe::cycle(unsigned step, std::string& error) {
    const unsigned value = step % 251;
    const float r = static_cast<float>(value) / 255.0f;
    const float g = static_cast<float>(255 - value) / 255.0f;
    D3d11Tools::clear(device_, view_.get(), r, g, 0.5f, 1.0f);
    std::vector<std::uint8_t> pixels;
    if (!D3d11Tools::read_pixels(device_, target_.get(), pixels, error)) {
        error = "readback failed: " + error;
        return false;
    }
    const std::size_t middle = (static_cast<std::size_t>(kSide / 2) * kSide + kSide / 2) * 4;
    const int expected_r = static_cast<int>(r * 255.0f + 0.5f);
    const int expected_g = static_cast<int>(g * 255.0f + 0.5f);
    if (pixels.size() < middle + 4 || std::abs(pixels[middle] - expected_r) > 1 || std::abs(pixels[middle + 1] - expected_g) > 1 ||
        std::abs(pixels[middle + 2] - 128) > 1) {
        error = "readback returned a wrong colour in cycle " + std::to_string(step);
        return false;
    }
    return true;
}

}  // namespace lsw::suites
