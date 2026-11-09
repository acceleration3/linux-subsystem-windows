#include "render/common/judge_selftest.hpp"

#include "render/common/case.hpp"
#include "render/common/diff.hpp"

#include <cmath>
#include <limits>

namespace lsw::render {

namespace {

constexpr std::uint32_t kDark = 0xff140a0au;
constexpr std::uint32_t kOrange = 0xff0080ffu;
constexpr std::uint32_t kWhite = 0xffffffffu;

Image triangle(int shift) {
    Image image = Image::solid(128, 128, kDark);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const int px = x - shift;
            const int py = y - shift;
            if (px >= 20 && py >= 20 && (px - 20) + (py - 20) <= 90) {
                image.set_rgba8(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), kOrange);
            }
        }
    }
    return image;
}

}  // namespace

std::vector<std::string> JudgeSelfTest::run(std::vector<std::string>& passed) {
    std::vector<std::string> failures;
    const auto expect = [&](const char* name, bool result, bool wanted) {
        (result == wanted ? passed : failures).push_back(std::string(name) + (result == wanted ? "" : wanted ? " should pass but failed" : " should fail but passed"));
    };

    {
        Image expected = Image::solid(128, 128, kDark);
        for (std::uint32_t y = 10; y < 110; ++y) {
            expected.set_rgba8(64, y, kWhite);
        }
        const Image actual = Image::solid(128, 128, kDark);
        expect("missing one pixel line fails under raster", ImageDiff::compare(expected, actual, Tolerance::raster(1, 1)).pass, false);
    }
    {
        expect("whole image shift by one pixel fails under raster", ImageDiff::compare(triangle(0), triangle(1), Tolerance::raster(1, 1)).pass, false);
    }
    {
        Image expected(32, 32, PixelKind::R8Uint);
        Image actual(32, 32, PixelKind::R8Uint);
        for (std::uint32_t y = 0; y < 32; ++y) {
            for (std::uint32_t x = 0; x < 32; ++x) {
                expected.set_channel(x, y, 0, 2);
                actual.set_channel(x, y, 0, x < 16 ? 2 : 3);
            }
        }
        expect("stencil 2 against 3 fails under tight", ImageDiff::compare(expected, actual, Tolerance::tight(1)).pass, false);
        expect("stencil 2 against 3 fails under psnr", ImageDiff::compare(expected, actual, Tolerance::psnr(40)).pass, false);
        expect("identical stencil passes", ImageDiff::compare(expected, expected, Tolerance::exact()).pass, true);
    }
    {
        Image expected(64, 64, PixelKind::Depth32Float);
        Image actual(64, 64, PixelKind::Depth32Float);
        for (std::uint32_t y = 0; y < 64; ++y) {
            for (std::uint32_t x = 0; x < 64; ++x) {
                expected.set_channel(x, y, 0, 0.500);
                actual.set_channel(x, y, 0, 0.503);
            }
        }
        expect("depth 0.500 against 0.503 fails under tight", ImageDiff::compare(expected, actual, Tolerance::tight(1)).pass, false);
        const CaseSpec spec("g", "n");
        expect("depth 0.500 against 0.503 fails under the case default", ImageDiff::compare(expected, actual, spec.resolve("depth", PixelKind::Depth32Float)).pass, false);
    }
    {
        Image expected = Image::solid(128, 128, kDark);
        for (std::uint32_t y = 0; y < 128; ++y) {
            for (std::uint32_t x = 0; x < 128; ++x) {
                expected.set_rgba8(x, y, 0xff000000u | (x << 8) | y);
            }
        }
        Image with_sprite = expected;
        for (std::uint32_t y = 40; y < 42; ++y) {
            for (std::uint32_t x = 40; x < 42; ++x) {
                with_sprite.set_rgba8(x, y, kWhite);
            }
        }
        expect("missing 2x2 white sprite fails under filter", ImageDiff::compare(with_sprite, expected, Tolerance::filter(4, 0.001)).pass, false);
        Image noisy = expected;
        noisy.set_rgba8(10, 10, noisy.rgba8(10, 10) ^ 0x00020202u);
        expect("a two level texel difference passes under filter", ImageDiff::compare(expected, noisy, Tolerance::filter(4, 0.001)).pass, true);
    }
    {
        Image image(8, 8, PixelKind::R32Float);
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                image.set_channel(x, y, 0, (x + y) % 3 == 0 ? std::numeric_limits<double>::infinity() : 1.0);
            }
        }
        expect("identical infinities pass", ImageDiff::compare(image, image, Tolerance::exact()).pass, true);
        Image other = image;
        other.set_channel(0, 0, 0, -std::numeric_limits<double>::infinity());
        expect("opposite infinities fail", ImageDiff::compare(image, other, Tolerance::floats(1e-3f, 1e-3f)).pass, false);
    }
    {
        const Image expected = triangle(0);
        Image actual = expected;
        int flipped = 0;
        for (std::uint32_t y = 25; y < 60 && flipped < 20; ++y) {
            for (std::uint32_t x = 20; x < 128; ++x) {
                if (expected.rgba8(x, y) == kOrange && expected.rgba8(x + 1, y) == kDark) {
                    actual.set_rgba8(x, y, kDark);
                    ++flipped;
                    break;
                }
            }
        }
        expect("one pixel edge jitter on a triangle passes under raster", ImageDiff::compare(expected, actual, Tolerance::raster(1, 1)).pass, true);
        expect("the same jitter fails under tight", ImageDiff::compare(expected, actual, Tolerance::tight(1)).pass, false);
        expect("identical triangles pass under exact", ImageDiff::compare(expected, expected, Tolerance::exact()).pass, true);
    }
    return failures;
}

}  // namespace lsw::render
