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
        Image expected(32, 32, PixelKind::Rgba16Float);
        Image near_image(32, 32, PixelKind::Rgba16Float);
        Image far_image(32, 32, PixelKind::Rgba16Float);
        for (std::uint32_t y = 0; y < 32; ++y) {
            for (std::uint32_t x = 0; x < 32; ++x) {
                for (unsigned c = 0; c < 4; ++c) {
                    expected.set_channel(x, y, c, 0.5);
                    near_image.set_channel(x, y, c, 0.5 + 0.0005);
                    far_image.set_channel(x, y, c, 0.5 + 0.06);
                }
            }
        }
        const auto judged = [&](const CaseSpec& spec, const Image& actual) {
            return ImageDiff::compare(expected, actual, spec.resolve("c", PixelKind::Rgba16Float)).pass;
        };
        const CaseSpec unset("g", "n");
        expect("an unset tolerance gives a float capture the float default", judged(unset, near_image), true);
        CaseSpec exact("g", "n");
        exact.tolerance(Tolerance::exact());
        expect("an explicit exact tolerance is exact on a float capture", judged(exact, near_image), false);
        CaseSpec tight("g", "n");
        tight.tolerance(Tolerance::tight(1));
        expect("tight(1) on a float capture allows one byte level", judged(tight, near_image), true);
        expect("tight(1) on a float capture rejects fifteen byte levels", judged(tight, far_image), false);
        CaseSpec loose("g", "n");
        loose.tolerance(Tolerance::psnr(40));
        expect("psnr(40) on a float capture accepts a tiny error", judged(loose, near_image), true);
        expect("psnr(40) on a float capture rejects a large error", judged(loose, far_image), false);
        CaseSpec overridden("g", "n");
        overridden.tolerance(Tolerance::tight(1)).tolerance_for("c", Tolerance::exact());
        expect("a per label exact override is exact on a float capture", judged(overridden, near_image), false);
        expect("an explicit exact tolerance reports itself as set", exact.tolerance_set() && !unset.tolerance_set(), true);
    }
    {
        Image expected(32, 32, PixelKind::R8Unorm);
        Image one_step(32, 32, PixelKind::R8Unorm);
        Image five_steps(32, 32, PixelKind::R8Unorm);
        for (std::uint32_t y = 0; y < 32; ++y) {
            for (std::uint32_t x = 0; x < 32; ++x) {
                expected.set_channel(x, y, 0, 127);
                one_step.set_channel(x, y, 0, 128);
                five_steps.set_channel(x, y, 0, 132);
            }
        }
        const Tolerance one_level = Tolerance::floats(1.0f / 255.0f + 1e-4f);
        expect("one level step on an 8 bit unorm capture passes under floats of one level", ImageDiff::compare(expected, one_step, one_level).pass, true);
        expect("five level steps on an 8 bit unorm capture fail under floats of one level", ImageDiff::compare(expected, five_steps, one_level).pass, false);
        expect("one level step on an 8 bit unorm capture fails under exact", ImageDiff::compare(expected, one_step, Tolerance::exact()).pass, false);
        Image wide(32, 32, PixelKind::R16Unorm);
        Image wide_step(32, 32, PixelKind::R16Unorm);
        Image wide_far(32, 32, PixelKind::R16Unorm);
        for (std::uint32_t y = 0; y < 32; ++y) {
            for (std::uint32_t x = 0; x < 32; ++x) {
                wide.set_channel(x, y, 0, 30000);
                wide_step.set_channel(x, y, 0, 30200);
                wide_far.set_channel(x, y, 0, 31500);
            }
        }
        expect("a step below one 8 bit level on a 16 bit unorm capture passes under floats of one level", ImageDiff::compare(wide, wide_step, one_level).pass, true);
        expect("a step of several 8 bit levels on a 16 bit unorm capture fails under floats of one level", ImageDiff::compare(wide, wide_far, one_level).pass, false);
    }
    {
        Image expected(32, 32, PixelKind::R8Unorm);
        Image wrapped(32, 32, PixelKind::R8Unorm);
        Image apart(32, 32, PixelKind::R8Unorm);
        for (std::uint32_t y = 0; y < 32; ++y) {
            for (std::uint32_t x = 0; x < 32; ++x) {
                expected.set_channel(x, y, 0, 254);
                wrapped.set_channel(x, y, 0, 1);
                apart.set_channel(x, y, 0, 100);
            }
        }
        const Tolerance wrapping = Tolerance::tight(4).with_wrap();
        expect("a sawtooth that wrapped from 254 to 1 passes under a wrap aware tolerance", ImageDiff::compare(expected, wrapped, wrapping).pass, true);
        expect("a sawtooth that wrapped from 254 to 1 fails without wrap awareness", ImageDiff::compare(expected, wrapped, Tolerance::tight(4)).pass, false);
        expect("a value far from the sawtooth fails under a wrap aware tolerance", ImageDiff::compare(expected, apart, wrapping).pass, false);
    }
    {
        Image expected(8, 8, PixelKind::Rgba32Float);
        Image alpha_step(8, 8, PixelKind::Rgba32Float);
        Image alpha_far(8, 8, PixelKind::Rgba32Float);
        Image color_step(8, 8, PixelKind::Rgba32Float);
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                for (unsigned c = 0; c < 4; ++c) {
                    expected.set_channel(x, y, c, c < 3 ? 0.5 : 2.0 / 3.0);
                    alpha_step.set_channel(x, y, c, c < 3 ? 0.5 : 1.0);
                    alpha_far.set_channel(x, y, c, c < 3 ? 0.5 : 0.0);
                    color_step.set_channel(x, y, c, c < 3 ? 0.5 + 3.0 / 1023.0 : 2.0 / 3.0);
                }
            }
        }
        const Tolerance per_channel = Tolerance::floats(2.0f / 1023.0f + 1e-5f).with_channel_absolute(0.0f, 0.0f, 0.0f, 1.0f / 3.0f + 1e-5f);
        expect("a step of the alpha channel passes under its own channel tolerance", ImageDiff::compare(expected, alpha_step, per_channel).pass, true);
        expect("two steps of the alpha channel fail under its own channel tolerance", ImageDiff::compare(expected, alpha_far, per_channel).pass, false);
        expect("a step of the alpha channel fails under the colour tolerance alone", ImageDiff::compare(expected, alpha_step, Tolerance::floats(2.0f / 1023.0f + 1e-5f)).pass, false);
        expect("three steps of a colour channel fail under its channel tolerance", ImageDiff::compare(expected, color_step, per_channel).pass, false);
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
    {
        const auto points = [&](std::uint32_t at, std::uint32_t size) {
            Image image = Image::solid(128, 128, kDark);
            for (std::uint32_t y = 0; y < size; ++y) {
                for (std::uint32_t x = 0; x < size; ++x) {
                    image.set_rgba8(at + x, at + y, kWhite);
                }
            }
            return image;
        };
        const Image expected = points(60, 2);
        expect("a size 2 point against itself passes under raster", ImageDiff::compare(expected, points(60, 2), Tolerance::raster(1, 1)).pass, true);
        expect("a size 2 point against size 1 fails under raster", ImageDiff::compare(expected, points(60, 1), Tolerance::raster(1, 1)).pass, false);
        expect("a size 2 point against size 3 fails under raster", ImageDiff::compare(expected, points(60, 3), Tolerance::raster(1, 1)).pass, false);
        expect("a size 2 point shifted by one pixel fails under raster", ImageDiff::compare(expected, points(61, 2), Tolerance::raster(1, 1)).pass, false);
    }
    return failures;
}

}  // namespace lsw::render
