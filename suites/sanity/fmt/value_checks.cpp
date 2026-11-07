#include "sanity/fmt/value_checks.hpp"

#include "core/text.hpp"
#include "sanity/cl/image_kit.hpp"
#include "sanity/fmt/format_data.hpp"
#include "sanity/fmt/gl_format_io.hpp"
#include "sanity/fmt/kit.hpp"
#include "sanity/fmt/vulkan_format_io.hpp"
#include "sanity/gl/kit.hpp"
#include "sanity/vk/kit.hpp"
#include "support/d3d11_format_runner.hpp"
#include "support/d3d12_format_runner.hpp"
#include "support/half_float.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

enum class Special { PositiveZero, NegativeZero, PositiveInfinity, NegativeInfinity, QuietNan, PayloadNan, MinDenormal, MaxDenormal, MinNormal, MaxNormal, One, MinusOne };

constexpr int kSpecials = 12;

const char* special_name(int index) {
    static const char* names[kSpecials] = {"+0", "-0", "+inf", "-inf", "qNaN", "NaN with payload", "smallest denormal", "largest denormal",
                                           "smallest normal", "largest normal", "1.0", "-1.0"};
    return names[index];
}

std::uint32_t single_bits(int index) {
    static const std::uint32_t bits[kSpecials] = {0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x7FC12345u,
                                                  0x00000001u, 0x007FFFFFu, 0x00800000u, 0x7F7FFFFFu, 0x3F800000u, 0xBF800000u};
    return bits[index];
}

std::uint16_t half_bits(int index) {
    static const std::uint16_t bits[kSpecials] = {0x0000, 0x8000, 0x7C00, 0xFC00, 0x7E00, 0x7E01, 0x0001, 0x03FF, 0x0400, 0x7BFF, 0x3C00, 0xBC00};
    return bits[index];
}

bool is_nan_single(std::uint32_t bits) { return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0; }

bool is_denormal_single(std::uint32_t bits) { return (bits & 0x7F800000u) == 0 && (bits & 0x007FFFFFu) != 0; }

struct Layout {
    DXGI_FORMAT format;
    int channels;
    bool half;
};

const Layout kLayouts[] = {{DXGI_FORMAT_R16_FLOAT, 1, true},          {DXGI_FORMAT_R16G16_FLOAT, 2, true},
                           {DXGI_FORMAT_R16G16B16A16_FLOAT, 4, true}, {DXGI_FORMAT_R32_FLOAT, 1, false},
                           {DXGI_FORMAT_R32G32_FLOAT, 2, false},      {DXGI_FORMAT_R32G32B32A32_FLOAT, 4, false}};

std::vector<std::uint8_t> special_texels(const Layout& layout) {
    std::vector<std::uint8_t> data;
    for (int t = 0; t < kSpecials; ++t) {
        for (int c = 0; c < layout.channels; ++c) {
            const int index = (t + c) % kSpecials;
            if (layout.half) {
                const std::uint16_t bits = half_bits(index);
                data.push_back(static_cast<std::uint8_t>(bits));
                data.push_back(static_cast<std::uint8_t>(bits >> 8));
            } else {
                const std::uint32_t bits = single_bits(index);
                for (int b = 0; b < 4; ++b) {
                    data.push_back(static_cast<std::uint8_t>(bits >> (8 * b)));
                }
            }
        }
    }
    return data;
}

std::uint32_t widen(const Layout& layout, int index) {
    if (!layout.half) {
        return single_bits(index);
    }
    const float value = HalfFloat::to_float(half_bits(index));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

bool sample_value_ok(const Layout& layout, int index, std::uint32_t got, std::string& why) {
    const std::uint32_t want = widen(layout, index);
    const bool nan = layout.half ? HalfFloat::is_nan(half_bits(index)) : is_nan_single(want);
    if (nan) {
        if (!is_nan_single(got)) {
            why = std::string(special_name(index)) + " sampled as 0x" + Text::hex(got, 8) + ", expected a NaN";
            return false;
        }
        return true;
    }
    const bool denormal = layout.half ? HalfFloat::is_denormal(half_bits(index)) : is_denormal_single(want);
    if (denormal) {
        if (got != want && (got & 0x7FFFFFFFu) != 0) {
            why = std::string(special_name(index)) + " sampled as 0x" + Text::hex(got, 8) + ", expected 0x" + Text::hex(want, 8) + " or a zero";
            return false;
        }
        return true;
    }
    if (got != want) {
        why = std::string(special_name(index)) + " sampled as 0x" + Text::hex(got, 8) + ", expected 0x" + Text::hex(want, 8);
        return false;
    }
    return true;
}

std::string table_prelude(const Layout& layout) {
    std::string text = "static const uint kBits[" + std::to_string(kSpecials) + "] = {";
    for (int i = 0; i < kSpecials; ++i) {
        text += "0x" + Text::hex(widen(layout, i), 8) + "u" + (i + 1 < kSpecials ? ", " : "");
    }
    return text + "};\n";
}

void runner_part(Context& context, FormatRunner& runner, Problems& problems, int& tested) {
    for (const Layout& layout : kLayouts) {
        UINT bits = 0;
        if (!runner.support(layout.format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        ++tested;
        const std::string name = std::string(runner.label()) + " " + FormatTable::name(layout.format);
        const std::vector<std::uint8_t> data = special_texels(layout);
        std::string error;
        std::vector<std::uint8_t> back;
        if (!runner.roundtrip(layout.format, kSpecials, 1, data, back, error)) {
            problems.add(name + " copy failed: " + error);
        } else if (back != data) {
            problems.add(name + " special values changed in a copy at byte " + std::to_string(Kit::first_difference(data, back)));
            ArtifactWriter writer(context);
            writer.bytes("fmt.special." + std::string(runner.label()) + "." + FormatTable::name(layout.format) + ".expected", data.data(), data.size());
            writer.bytes("fmt.special." + std::string(runner.label()) + "." + FormatTable::name(layout.format) + ".actual", back.data(), back.size());
        }
        if ((bits & Kit::kSampleBits) != 0) {
            std::vector<std::uint32_t> words;
            if (!runner.sample(layout.format, layout.format, "float4", kSpecials, 1, data, words, error)) {
                problems.add(name + " sampling failed: " + error);
            } else {
                for (int t = 0; t < kSpecials; ++t) {
                    for (int c = 0; c < layout.channels; ++c) {
                        std::string why;
                        if (!sample_value_ok(layout, (t + c) % kSpecials, words[static_cast<std::size_t>(t) * 4 + static_cast<std::size_t>(c)], why)) {
                            problems.add(name + " texel " + std::to_string(t) + " channel " + std::to_string(c) + ": " + why);
                        }
                    }
                }
            }
        }
        if ((bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
            std::string expression = "float4(asfloat(kBits[uint(pos.x) % " + std::to_string(kSpecials) + "u]), asfloat(kBits[(uint(pos.x) + 1u) % " +
                                     std::to_string(kSpecials) + "u]), asfloat(kBits[(uint(pos.x) + 2u) % " + std::to_string(kSpecials) +
                                     "u]), asfloat(kBits[(uint(pos.x) + 3u) % " + std::to_string(kSpecials) + "u]))";
            if (!runner.draw(layout.format, kSpecials, 1, "float4", expression, nullptr, false, back, error, table_prelude(layout))) {
                problems.add(name + " draw failed: " + error);
                continue;
            }
            for (int t = 0; t < kSpecials; ++t) {
                for (int c = 0; c < layout.channels; ++c) {
                    const int index = (t + c) % kSpecials;
                    std::uint32_t got = 0;
                    if (layout.half) {
                        std::uint16_t h = 0;
                        std::memcpy(&h, back.data() + (static_cast<std::size_t>(t) * static_cast<std::size_t>(layout.channels) + static_cast<std::size_t>(c)) * 2, 2);
                        const float value = HalfFloat::to_float(h);
                        std::memcpy(&got, &value, 4);
                    } else {
                        std::memcpy(&got, back.data() + (static_cast<std::size_t>(t) * static_cast<std::size_t>(layout.channels) + static_cast<std::size_t>(c)) * 4, 4);
                    }
                    std::string why;
                    if (!sample_value_ok(layout, index, got, why)) {
                        why = "rendered: " + why;
                    }
                    if (!why.empty()) {
                        problems.add(name + " texel " + std::to_string(t) + " channel " + std::to_string(c) + ": " + why);
                    }
                }
            }
        }
    }
}

void d3d11_part(Context& context, Problems& problems) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error)) {
        problems.add("D3D11: " + error);
        return;
    }
    D3d11FormatRunner runner(device);
    int tested = 0;
    runner_part(context, runner, problems, tested);
    context.measure("fmt.special.d3d11.formats", tested);
}

void d3d12_part(Context& context, Problems& problems) {
    D3d12Rig rig;
    std::string error;
    if (!Kit::open_d3d12(rig, error)) {
        problems.add("D3D12: " + error);
        return;
    }
    D3d12FormatRunner runner(rig);
    int tested = 0;
    runner_part(context, runner, problems, tested);
    context.measure("fmt.special.d3d12.formats", tested);
}

bool copy_equal(const Layout& layout, const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& back, bool payload, std::string& why) {
    if (back.size() != data.size()) {
        why = "the readback has " + std::to_string(back.size()) + " bytes, expected " + std::to_string(data.size());
        return false;
    }
    const std::size_t width = layout.half ? 2 : 4;
    for (std::size_t offset = 0; offset + width <= data.size(); offset += width) {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::memcpy(&a, data.data() + offset, width);
        std::memcpy(&b, back.data() + offset, width);
        if (a == b) {
            continue;
        }
        bool both_nan = false;
        if (layout.half) {
            both_nan = HalfFloat::is_nan(static_cast<std::uint16_t>(a)) && HalfFloat::is_nan(static_cast<std::uint16_t>(b));
        } else {
            both_nan = is_nan_single(a) && is_nan_single(b);
        }
        if (both_nan && !payload) {
            continue;
        }
        why = "value at byte " + std::to_string(offset) + " is 0x" + Text::hex(b, static_cast<int>(width) * 2) + ", expected 0x" + Text::hex(a, static_cast<int>(width) * 2);
        return false;
    }
    return true;
}

Outcome vulkan_part(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        for (const Layout& layout : kLayouts) {
            const FormatEntry* entry = FormatTable::find(layout.format);
            const VkFormat format = static_cast<VkFormat>(entry->vulkan);
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (!VulkanFormatIo::image_ok(rig, format, usage, kSpecials, 1)) {
                continue;
            }
            const std::vector<std::uint8_t> data = special_texels(layout);
            VulkanImage image;
            std::vector<std::uint8_t> back;
            VkResult result = VulkanFormatIo::make(rig, image, format, kSpecials, 1, usage);
            if (result == VK_SUCCESS) {
                result = VulkanFormatIo::upload(rig, image, data, kSpecials, 1);
            }
            if (result == VK_SUCCESS) {
                result = VulkanFormatIo::download(rig, image, data.size(), kSpecials, 1, back);
            }
            std::string why;
            if (result != VK_SUCCESS) {
                problems.add(std::string("Vulkan ") + entry->name + " copy failed: " + vk::Kit::name(result));
            } else if (!copy_equal(layout, data, back, true, why)) {
                problems.add(std::string("Vulkan ") + entry->name + " special values changed in a copy: " + why);
            }
        }
        return {};
    });
}

void gl_part(Context& context, Problems& problems) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        problems.add("OpenGL: " + error);
        return;
    }
    for (const Layout& layout : kLayouts) {
        const FormatEntry* entry = FormatTable::find(layout.format);
        if (!GlFormatIo::reported_supported(rig.gl, entry->gl_internal)) {
            continue;
        }
        const std::vector<std::uint8_t> data = special_texels(layout);
        std::vector<std::uint8_t> back;
        const GlFormatIo::Result result = GlFormatIo::upload_read(rig.gl, *entry, kSpecials, 1, data, back, error);
        std::string why;
        if (result != GlFormatIo::Result::Done) {
            problems.add(std::string("OpenGL ") + entry->name + " upload failed: " + error);
        } else if (!copy_equal(layout, data, back, false, why)) {
            problems.add(std::string("OpenGL ") + entry->name + " special values changed in a copy: " + why);
        }
    }
}

Outcome opencl_part(Context& context) {
    return cl::Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        if (!rig.images()) {
            return;
        }
        const struct {
            cl_channel_order order;
            cl_channel_type type;
            Layout layout;
        } cases[] = {{CL_R, CL_FLOAT, {DXGI_FORMAT_R32_FLOAT, 1, false}},
                     {CL_RGBA, CL_FLOAT, {DXGI_FORMAT_R32G32B32A32_FLOAT, 4, false}},
                     {CL_R, CL_HALF_FLOAT, {DXGI_FORMAT_R16_FLOAT, 1, true}},
                     {CL_RGBA, CL_HALF_FLOAT, {DXGI_FORMAT_R16G16B16A16_FLOAT, 4, true}}};
        for (const auto& item : cases) {
            cl_image_format format{item.order, item.type};
            cl::ImageShape shape;
            shape.type = CL_MEM_OBJECT_IMAGE2D;
            shape.width = kSpecials;
            shape.height = 1;
            const std::vector<std::uint8_t> data = special_texels(item.layout);
            std::vector<std::uint8_t> host = data;
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> image = cl::ImageKit::create(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, format, shape, host.data(), code);
            if (!image) {
                continue;
            }
            std::vector<std::uint8_t> back(data.size(), 0xCD);
            const std::size_t origin[3] = {0, 0, 0};
            const std::size_t region[3] = {kSpecials, 1, 1};
            code = rig.api.EnqueueReadImage(rig.queue, image.get(), CL_TRUE, origin, region, 0, 0, back.data(), 0, nullptr, nullptr);
            std::string why;
            if (code != CL_SUCCESS) {
                problems.add("OpenCL " + ClImageFormats::name(format) + " readback failed with " + ClTools::error_name(code));
            } else if (!copy_equal(item.layout, data, back, false, why)) {
                problems.add("OpenCL " + ClImageFormats::name(format) + " special values changed in a transfer: " + why);
            }
        }
    });
}

void merge(const char* api, const Outcome& outcome, Problems& problems, std::vector<std::string>& skips, bool& exercised) {
    if (outcome.status() == Status::Fail) {
        problems.add(std::string(api) + ": " + outcome.reason());
    } else if (outcome.status() == Status::Skip) {
        skips.push_back(std::string(api) + ": " + outcome.reason());
    } else {
        exercised = true;
    }
}

struct ClampCase {
    DXGI_FORMAT format;
    const char* type;
    const char* expression;
    double want[4];
};

const ClampCase kClampCases[] = {
    {DXGI_FORMAT_R8G8B8A8_UNORM, "float4", "float4(-0.5, 1.5, 7.0, 2.0)", {0, 1, 1, 1}},
    {DXGI_FORMAT_R16G16_UNORM, "float4", "float4(-100.0, 3.0, 0, 1)", {0, 1, 0, 1}},
    {DXGI_FORMAT_R8_UNORM, "float4", "float4(5.0, 0, 0, 1)", {1, 0, 0, 1}},
    {DXGI_FORMAT_R10G10B10A2_UNORM, "float4", "float4(-1.0, 9.0, 0.5, 2.5)", {0, 1, 0.5, 1}},
    {DXGI_FORMAT_R8G8B8A8_SNORM, "float4", "float4(-3.0, 3.0, -1.0, 1.0)", {-1, 1, -1, 1}},
    {DXGI_FORMAT_R16G16B16A16_SNORM, "float4", "float4(-3.0, 3.0, -1.0, 1.0)", {-1, 1, -1, 1}},
    {DXGI_FORMAT_R8G8_SNORM, "float4", "float4(-9.0, 9.0, 0, 0)", {-1, 1, 0, 0}},
    {DXGI_FORMAT_R8_SNORM, "float4", "float4(-9.0, 0, 0, 0)", {-1, 0, 0, 0}},
    {DXGI_FORMAT_R8G8B8A8_UINT, "uint4", "uint4(300, 70000, 5, 4000000000u)", {255, 255, 5, 255}},
    {DXGI_FORMAT_R16G16_UINT, "uint4", "uint4(70000, 5, 0, 1)", {65535, 5, 0, 1}},
    {DXGI_FORMAT_R8G8B8A8_SINT, "int4", "int4(300, -300, 5, -5)", {127, -128, 5, -5}},
    {DXGI_FORMAT_R16_SINT, "int4", "int4(40000, 0, 0, 1)", {32767, 0, 0, 1}},
    {DXGI_FORMAT_R10G10B10A2_UINT, "uint4", "uint4(2000, 5, 1023, 9)", {1023, 5, 1023, 3}}};

void clamp_runner(FormatRunner& runner, Problems& problems, int& tested) {
    for (const ClampCase& c : kClampCases) {
        UINT bits = 0;
        if (!runner.support(c.format, bits) || (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
            continue;
        }
        ++tested;
        const FormatSpec spec = FormatCodec::spec(c.format);
        const std::string name = std::string(runner.label()) + " " + FormatTable::name(c.format);
        std::vector<std::uint8_t> back;
        std::string error;
        if (!runner.draw(c.format, 4, 4, c.type, c.expression, nullptr, false, back, error)) {
            problems.add(name + " draw failed: " + error);
            continue;
        }
        std::string why;
        if (!FormatCodec::matches(spec, back.data() + static_cast<std::size_t>(spec.bytes) * 5, c.want, why)) {
            problems.add(name + " did not clamp " + c.expression + ": " + why);
        }
    }
    const DXGI_FORMAT symmetric[] = {DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R16G16B16A16_SNORM};
    for (DXGI_FORMAT format : symmetric) {
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
            continue;
        }
        const FormatSpec spec = FormatCodec::spec(format);
        std::vector<std::uint8_t> back;
        std::string error;
        const std::string name = std::string(runner.label()) + " " + FormatTable::name(format);
        if (!runner.draw(format, 4, 4, "float4", "float4(-0.3, 0.3, -0.7, 0.7)", nullptr, false, back, error)) {
            problems.add(name + " symmetric draw failed: " + error);
            continue;
        }
        const std::size_t texel = static_cast<std::size_t>(spec.bytes) * 5;
        const int bits_per = spec.field[0].bits;
        auto code = [&](int channel) {
            std::uint64_t raw = FormatData::read_field(back.data() + texel, spec.field[channel].shift, bits_per);
            std::int64_t value = static_cast<std::int64_t>(raw);
            if ((raw >> (bits_per - 1)) & 1) {
                value -= 1ll << bits_per;
            }
            return value;
        };
        if (code(0) != -code(1) || code(2) != -code(3)) {
            problems.add(name + " stores -x and +x asymmetrically: " + std::to_string(code(0)) + "/" + std::to_string(code(1)) + " and " +
                         std::to_string(code(2)) + "/" + std::to_string(code(3)));
        }
        const std::int64_t expected_first = static_cast<std::int64_t>(std::llround(0.3 * static_cast<double>((1ll << (bits_per - 1)) - 1)));
        if (std::llabs(code(1) - expected_first) > 1) {
            problems.add(name + " stored 0.3 as code " + std::to_string(code(1)) + ", expected " + std::to_string(expected_first));
        }
    }
    const DXGI_FORMAT minimum_formats[] = {DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R16G16B16A16_SNORM};
    for (DXGI_FORMAT format : minimum_formats) {
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & Kit::kSampleBits) == 0) {
            continue;
        }
        const FormatSpec spec = FormatCodec::spec(format);
        std::vector<std::uint8_t> data(static_cast<std::size_t>(spec.bytes) * 2, 0);
        const int field_bits = spec.field[0].bits;
        for (int channel = 0; channel < 4; ++channel) {
            FormatData::write_field(data.data(), spec.field[channel].shift, field_bits, 1ull << (field_bits - 1));
            FormatData::write_field(data.data() + spec.bytes, spec.field[channel].shift, field_bits, (1ull << (field_bits - 1)) + 1);
        }
        std::vector<std::uint32_t> words;
        std::string error;
        if (!runner.sample(format, format, "float4", 2, 1, data, words, error)) {
            problems.add(std::string(runner.label()) + " " + FormatTable::name(format) + " minimum code sampling failed: " + error);
            continue;
        }
        for (int texel = 0; texel < 2; ++texel) {
            for (int channel = 0; channel < 4; ++channel) {
                float value = 0.0f;
                std::memcpy(&value, &words[static_cast<std::size_t>(texel) * 4 + static_cast<std::size_t>(channel)], 4);
                if (value != -1.0f) {
                    problems.add(std::string(runner.label()) + " " + FormatTable::name(format) + " texel " + std::to_string(texel) +
                                 " (the code " + (texel == 0 ? "-2^(n-1)" : "-2^(n-1)+1") + ") sampled as " + std::to_string(value) + ", expected -1");
                    break;
                }
            }
        }
    }
}

}  // namespace

Outcome ValueChecks::float_special_values(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    if (Kit::enabled("d3d11")) {
        d3d11_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        d3d12_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("vulkan")) {
        merge("Vulkan", vulkan_part(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        gl_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("OpenGL excluded");
    }
    if (Kit::enabled("opencl")) {
        merge("OpenCL", opencl_part(context), problems, skips, exercised);
    } else {
        skips.push_back("OpenCL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

Outcome ValueChecks::snorm_and_clamp(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    if (Kit::enabled("d3d11")) {
        AvAdapter adapter;
        D3d11Device device;
        std::string error;
        if (!Kit::open_d3d11(adapter, device, error)) {
            problems.add("D3D11: " + error);
        } else {
            D3d11FormatRunner runner(device);
            int tested = 0;
            clamp_runner(runner, problems, tested);
            context.measure("fmt.clamp.d3d11.cases", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        D3d12Rig rig;
        std::string error;
        if (!Kit::open_d3d12(rig, error)) {
            problems.add("D3D12: " + error);
        } else {
            D3d12FormatRunner runner(rig);
            int tested = 0;
            clamp_runner(runner, problems, tested);
            context.measure("fmt.clamp.d3d12.cases", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("vulkan")) {
        merge("Vulkan", vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& found) -> std::string {
                  const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R16G16B16A16_UNORM,
                                                 DXGI_FORMAT_R16G16B16A16_SNORM};
                  const double values[4] = {-3.0, 5.0, 0.0, 9.0};
                  for (DXGI_FORMAT dxgi : formats) {
                      const FormatEntry* entry = FormatTable::find(dxgi);
                      const FormatSpec spec = FormatCodec::spec(dxgi);
                      const VkFormat format = static_cast<VkFormat>(entry->vulkan);
                      const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                      if (!VulkanFormatIo::image_ok(rig, format, usage, 4, 4)) {
                          continue;
                      }
                      VulkanImage image;
                      std::vector<std::uint8_t> back;
                      VkResult result = VulkanFormatIo::make(rig, image, format, 4, 4, usage);
                      if (result == VK_SUCCESS) {
                          result = VulkanFormatIo::clear(rig, image, VulkanFormatIo::clear_value(spec, values));
                      }
                      if (result == VK_SUCCESS) {
                          result = VulkanFormatIo::download(rig, image, static_cast<std::size_t>(spec.bytes) * 16, 4, 4, back);
                      }
                      const double want[4] = {spec.kind == FormatKind::Snorm ? -1.0 : 0.0, 1.0, 0.0, 1.0};
                      std::string why;
                      if (result != VK_SUCCESS) {
                          found.add(std::string("Vulkan ") + entry->name + " clear failed: " + vk::Kit::name(result));
                      } else if (!FormatCodec::matches(spec, back.data() + static_cast<std::size_t>(spec.bytes) * 5, want, why)) {
                          found.add(std::string("Vulkan ") + entry->name + " did not clamp an out-of-range clear colour: " + why);
                      }
                  }
                  return {};
              }),
              problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        GlRig rig;
        std::string error;
        if (!gl::Kit::open(rig, context, error, 64, 64)) {
            problems.add("OpenGL: " + error);
        } else {
            const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R16G16B16A16_UNORM,
                                           DXGI_FORMAT_R16G16B16A16_SNORM};
            const double values[4] = {-3.0, 5.0, 0.0, 9.0};
            for (DXGI_FORMAT dxgi : formats) {
                const FormatEntry* entry = FormatTable::find(dxgi);
                const FormatSpec spec = FormatCodec::spec(dxgi);
                if (!GlFormatIo::reported_supported(rig.gl, entry->gl_internal)) {
                    continue;
                }
                std::vector<std::uint8_t> back;
                const GlFormatIo::Result result = GlFormatIo::clear_read(rig.gl, *entry, spec, values, 4, 4, back, error);
                const double want[4] = {spec.kind == FormatKind::Snorm ? -1.0 : 0.0, 1.0, 0.0, 1.0};
                std::string why;
                if (result == GlFormatIo::Result::Failed) {
                    problems.add(std::string("OpenGL ") + entry->name + ": " + error);
                } else if (result == GlFormatIo::Result::Done &&
                           !FormatCodec::matches(spec, back.data() + static_cast<std::size_t>(spec.bytes) * 5, want, why)) {
                    problems.add(std::string("OpenGL ") + entry->name + " did not clamp an out-of-range clear colour: " + why);
                }
            }
        }
        exercised = true;
    } else {
        skips.push_back("OpenGL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
