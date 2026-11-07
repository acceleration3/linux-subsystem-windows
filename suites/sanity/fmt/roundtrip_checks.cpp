#include "sanity/fmt/roundtrip_checks.hpp"

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
#include "support/packed_codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

constexpr unsigned kWidth = 13;
constexpr unsigned kHeight = 7;

bool selected(const FormatEntry& entry, bool packed) {
    return !FormatCodec::is_block(entry.dxgi) && entry.packed == packed;
}

struct RenderCase {
    double values[4];
    std::string type;
    std::string expression;
    float clear[4];
};

std::string literal(double value) {
    return std::to_string(value);
}

RenderCase render_case(const FormatSpec& spec) {
    RenderCase c{};
    if (spec.kind == FormatKind::Uint) {
        const double v[4] = {5, 6, 7, 1};
        std::copy(v, v + 4, c.values);
        c.type = "uint4";
        c.expression = "uint4(5, 6, 7, 1)";
    } else if (spec.kind == FormatKind::Sint) {
        const double v[4] = {-3, 2, -1, 1};
        std::copy(v, v + 4, c.values);
        c.type = "int4";
        c.expression = "int4(-3, 2, -1, 1)";
    } else {
        double v[4] = {0.25, 0.5, 0.75, 1.0};
        if (spec.kind == FormatKind::Snorm) {
            v[0] = -0.5;
            v[2] = -0.25;
        } else if (spec.kind == FormatKind::Float && spec.field[0].bits >= 16 && spec.field[0].bits <= 32 && spec.count > 0 && spec.field[0].shift == 0 &&
                   spec.field[0].bits != 11) {
            v[1] = -0.5;
            v[2] = 1.5;
            v[3] = 100.0;
        }
        std::copy(v, v + 4, c.values);
        c.type = "float4";
        c.expression = "float4(" + literal(v[0]) + ", " + literal(v[1]) + ", " + literal(v[2]) + ", " + literal(v[3]) + ")";
    }
    for (int i = 0; i < 4; ++i) {
        c.clear[i] = static_cast<float>(c.values[i]);
    }
    return c;
}

void dump(Context& context, const std::string& name, const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual) {
    ArtifactWriter writer(context);
    writer.bytes(name + ".expected", expected.data(), expected.size());
    writer.bytes(name + ".actual", actual.data(), actual.size());
}

class Report {
public:
    explicit Report(Problems& problems) : problems_(problems) {}

    void add(const std::string& text) {
        if (++count_ <= 40) {
            problems_.add(text);
        }
    }

    [[nodiscard]] int count() const { return count_; }

private:
    Problems& problems_;
    int count_ = 0;
};

void runner_matrix(Context& context, FormatRunner& runner, bool packed, Report& report, int& tested) {
    for (const FormatEntry& entry : FormatTable::all()) {
        if (!selected(entry, packed)) {
            continue;
        }
        const DXGI_FORMAT format = entry.dxgi;
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        ++tested;
        const FormatSpec spec = FormatData::spec_of(format);
        const std::string name = std::string(runner.label()) + " " + entry.name;
        const std::vector<std::uint8_t> data = FormatData::make(format, 0x1000 + static_cast<std::uint64_t>(format), kWidth, kHeight);
        std::vector<std::uint8_t> back;
        std::string error;
        if (!runner.roundtrip(format, kWidth, kHeight, data, back, error)) {
            report.add(name + " upload and readback failed: " + error);
        } else if (back != data) {
            report.add(name + " upload, copy and readback changed byte " + std::to_string(Kit::first_difference(data, back)) + " of " +
                       std::to_string(data.size()));
            dump(context, "fmt." + std::string(runner.label()) + "." + entry.name, data, back);
        }
        if ((bits & Kit::kSampleBits) != 0) {
            const char* type = spec.kind == FormatKind::Uint ? "uint4" : (spec.kind == FormatKind::Sint ? "int4" : "float4");
            std::vector<std::uint32_t> words;
            if (!runner.sample(format, format, type, kWidth, kHeight, data, words, error)) {
                report.add(name + " sampling failed: " + error);
            } else {
                for (unsigned t = 0; t < kWidth * kHeight; ++t) {
                    double want[4];
                    FormatData::expected_texel(format, spec, data.data() + static_cast<std::size_t>(t) * static_cast<std::size_t>(spec.bytes), want);
                    std::string why;
                    if (!FormatData::sample_matches(spec, want, words.data() + static_cast<std::size_t>(t) * 4, why)) {
                        report.add(name + " texel " + std::to_string(t) + " samples wrongly: " + why);
                        break;
                    }
                }
            }
        }
        if ((bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0 && FormatCodec::spec(format).known) {
            const RenderCase c = render_case(spec);
            const std::size_t texel = static_cast<std::size_t>(spec.bytes) * 9;
            std::string why;
            const bool integer = spec.kind == FormatKind::Uint || spec.kind == FormatKind::Sint;
            if (!integer) {
                if (!runner.clear(format, 8, 8, c.clear, back, error)) {
                    report.add(name + " clear failed: " + error);
                } else if (back.size() < texel + static_cast<std::size_t>(spec.bytes) || !FormatCodec::matches(spec, back.data() + texel, c.values, why)) {
                    report.add(name + " cleared to the wrong value: " + why);
                }
            }
            if (!runner.draw(format, 8, 8, c.type, c.expression, nullptr, false, back, error)) {
                report.add(name + " draw failed: " + error);
            } else if (back.size() < texel + static_cast<std::size_t>(spec.bytes) || !FormatCodec::matches(spec, back.data() + texel, c.values, why)) {
                report.add(name + " drew the wrong value: " + why);
            }
        }
    }
}


struct PackedCase {
    DXGI_FORMAT format;
    int bytes;
    std::vector<std::uint32_t> words;
};

std::vector<PackedCase> packed_cases() {
    std::vector<PackedCase> cases;
    cases.push_back({DXGI_FORMAT_R10G10B10A2_UNORM, 4, {0x00000000u, 0xFFFFFFFFu, 0x000003FFu, 0x000FFC00u, 0x3FF00000u, 0xC0000000u, 0x15555555u, 0x2AAAAAAAu}});
    cases.push_back({DXGI_FORMAT_R10G10B10A2_UINT, 4, {0x00000000u, 0xFFFFFFFFu, 0x000003FFu, 0x000FFC00u, 0x3FF00000u, 0xC0000000u, 0x15555555u, 0x2AAAAAAAu}});
    PackedCase small{DXGI_FORMAT_R11G11B10_FLOAT, 4, {}};
    const std::uint32_t red[8] = {0x000, 0x3C0, 0x7C0, 0x001, 0x7BF, 0x040, 0x03F, 0x440};
    const std::uint32_t blue[8] = {0x000, 0x1E0, 0x3E0, 0x001, 0x3DF, 0x020, 0x01F, 0x220};
    for (int i = 0; i < 8; ++i) {
        small.words.push_back(red[i] | (red[i] << 11) | (blue[i] << 22));
    }
    cases.push_back(small);
    PackedCase shared{DXGI_FORMAT_R9G9B9E5_SHAREDEXP, 4, {}};
    const double colors[8][3] = {{1.0, 0.5, 0.25}, {0, 0, 0}, {65408.0, 0, 0}, {0.0001, 0.0002, 0.0004}, {1, 1, 1}, {0.75, 0.375, 0.1875}, {3, 2, 1}, {255, 128, 64}};
    for (const auto& color : colors) {
        shared.words.push_back(PackedCodec::encode_rgb9e5(color));
    }
    cases.push_back(shared);
    cases.push_back({DXGI_FORMAT_B5G6R5_UNORM, 2, {0x0000u, 0xFFFFu, 0xF800u, 0x07E0u, 0x001Fu, 0x8410u, 0x4208u, 0xAAAAu}});
    cases.push_back({DXGI_FORMAT_B5G5R5A1_UNORM, 2, {0x0000u, 0xFFFFu, 0x7C00u, 0x03E0u, 0x001Fu, 0x8000u, 0x4210u, 0xAAAAu}});
    cases.push_back({DXGI_FORMAT_B4G4R4A4_UNORM, 2, {0x0000u, 0xFFFFu, 0x0F00u, 0x00F0u, 0x000Fu, 0xF000u, 0x8421u, 0xAAAAu}});
    return cases;
}

void packed_analytic(Context& context, FormatRunner& runner, Report& report) {
    for (const PackedCase& c : packed_cases()) {
        UINT bits = 0;
        if (!runner.support(c.format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        const FormatSpec spec = FormatData::spec_of(c.format);
        const std::string name = std::string(runner.label()) + " " + FormatTable::name(c.format);
        std::vector<std::uint8_t> data;
        for (std::uint32_t word : c.words) {
            for (int b = 0; b < c.bytes; ++b) {
                data.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
            }
        }
        const unsigned count = static_cast<unsigned>(c.words.size());
        std::string error;
        std::vector<std::uint8_t> back;
        if (runner.roundtrip(c.format, count, 1, data, back, error)) {
            if (back != data) {
                report.add(name + " analytic texels changed in a copy at byte " + std::to_string(Kit::first_difference(data, back)));
            }
        } else {
            report.add(name + " analytic texels: upload failed: " + error);
        }
        if ((bits & Kit::kSampleBits) != 0) {
            const char* type = spec.kind == FormatKind::Uint ? "uint4" : "float4";
            std::vector<std::uint32_t> words;
            if (!runner.sample(c.format, c.format, type, count, 1, data, words, error)) {
                report.add(name + " analytic texels: sampling failed: " + error);
                continue;
            }
            for (unsigned t = 0; t < count; ++t) {
                double want[4];
                FormatData::expected_texel(c.format, spec, data.data() + static_cast<std::size_t>(t) * static_cast<std::size_t>(c.bytes), want);
                std::string why;
                if (!FormatData::sample_matches(spec, want, words.data() + static_cast<std::size_t>(t) * 4, why)) {
                    report.add(name + " texel " + std::to_string(t) + " (0x" + Text::hex(c.words[t], c.bytes * 2) + ") samples wrongly: " + why);
                }
            }
        }
        static_cast<void>(context);
    }
    UINT bits = 0;
    if (runner.support(DXGI_FORMAT_R11G11B10_FLOAT, bits) && (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
        const FormatSpec spec = FormatCodec::spec(DXGI_FORMAT_R11G11B10_FLOAT);
        std::vector<std::uint8_t> back;
        std::string error;
        if (!runner.draw(DXGI_FORMAT_R11G11B10_FLOAT, 4, 4, "float4", "float4(-0.5, asfloat(0x7f800000u), 0.75, 1)", nullptr, false, back, error)) {
            report.add(std::string(runner.label()) + " R11G11B10_FLOAT draw failed: " + error);
        } else {
            double got[4];
            FormatCodec::decode(spec, back.data() + 5 * 4, got);
            if (got[0] != 0.0 || !std::isinf(got[1]) || got[1] < 0.0 || got[2] != 0.75) {
                report.add(std::string(runner.label()) + " R11G11B10_FLOAT stored (" + std::to_string(got[0]) + ", " + std::to_string(got[1]) + ", " +
                           std::to_string(got[2]) + ") for (-0.5, +inf, 0.75): negative values must clamp to 0, infinity must survive");
            }
        }
    }
    if (runner.support(DXGI_FORMAT_R10G10B10A2_UINT, bits) && (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
        std::vector<std::uint8_t> back;
        std::string error;
        if (runner.draw(DXGI_FORMAT_R10G10B10A2_UINT, 4, 4, "uint4", "uint4(1023, 500, 7, 3)", nullptr, false, back, error)) {
            std::uint32_t word = 0;
            std::memcpy(&word, back.data() + 5 * 4, 4);
            const std::uint32_t want = 1023u | (500u << 10) | (7u << 20) | (3u << 30);
            if (word != want) {
                report.add(std::string(runner.label()) + " R10G10B10A2_UINT stored 0x" + Text::hex(word, 8) + " for (1023, 500, 7, 3), expected 0x" + Text::hex(want, 8));
            }
        } else {
            report.add(std::string(runner.label()) + " R10G10B10A2_UINT draw failed: " + error);
        }
    }
}

void d3d11_part(Context& context, bool packed, Problems& problems) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error)) {
        problems.add("D3D11: " + error);
        return;
    }
    D3d11FormatRunner runner(device);
    Report report(problems);
    int tested = 0;
    runner_matrix(context, runner, packed, report, tested);
    if (packed) {
        packed_analytic(context, runner, report);
    }
    context.measure("fmt.roundtrip.d3d11.formats", tested);
}

void d3d12_part(Context& context, bool packed, Problems& problems) {
    D3d12Rig rig;
    std::string error;
    if (!Kit::open_d3d12(rig, error)) {
        problems.add("D3D12: " + error);
        return;
    }
    D3d12FormatRunner runner(rig);
    Report report(problems);
    int tested = 0;
    runner_matrix(context, runner, packed, report, tested);
    if (packed) {
        packed_analytic(context, runner, report);
    }
    context.measure("fmt.roundtrip.d3d12.formats", tested);
}

Outcome vulkan_part(Context& context, bool packed) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        Report report(problems);
        int tested = 0;
        for (const FormatEntry& entry : FormatTable::all()) {
            if (!selected(entry, packed) || entry.vulkan == 0) {
                continue;
            }
            const VkFormat format = static_cast<VkFormat>(entry.vulkan);
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (!VulkanFormatIo::image_ok(rig, format, usage, kWidth, kHeight)) {
                continue;
            }
            ++tested;
            const FormatSpec spec = FormatData::spec_of(entry.dxgi);
            const std::string name = std::string("Vulkan ") + entry.name;
            const std::vector<std::uint8_t> data = FormatData::make(entry.dxgi, 0x2000 + static_cast<std::uint64_t>(entry.dxgi), kWidth, kHeight);
            VulkanImage image;
            VkResult result = VulkanFormatIo::make(rig, image, format, kWidth, kHeight, usage);
            if (result != VK_SUCCESS) {
                report.add(name + " image creation failed: " + vk::Kit::name(result));
                continue;
            }
            std::vector<std::uint8_t> back;
            result = VulkanFormatIo::upload(rig, image, data, kWidth, kHeight);
            if (result == VK_SUCCESS) {
                result = VulkanFormatIo::download(rig, image, data.size(), kWidth, kHeight, back);
            }
            if (result != VK_SUCCESS) {
                report.add(name + " upload or readback failed: " + vk::Kit::name(result));
            } else if (back != data) {
                report.add(name + " upload and readback changed byte " + std::to_string(Kit::first_difference(data, back)) + " of " +
                           std::to_string(data.size()));
                dump(context, std::string("fmt.vulkan.") + entry.name, data, back);
            }
            if (FormatCodec::spec(entry.dxgi).known) {
                const RenderCase c = render_case(spec);
                result = VulkanFormatIo::clear(rig, image, VulkanFormatIo::clear_value(spec, c.values));
                if (result == VK_SUCCESS) {
                    result = VulkanFormatIo::download(rig, image, data.size(), kWidth, kHeight, back);
                }
                std::string why;
                if (result != VK_SUCCESS) {
                    report.add(name + " clear or readback failed: " + vk::Kit::name(result));
                } else if (!FormatCodec::matches(spec, back.data() + static_cast<std::size_t>(spec.bytes) * 9, c.values, why)) {
                    report.add(name + " cleared to the wrong value: " + why);
                }
            }
        }
        context.measure("fmt.roundtrip.vulkan.formats", tested);
        return {};
    });
}

void gl_part(Context& context, bool packed, Problems& problems, std::vector<std::string>& skips) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        problems.add("OpenGL: " + error);
        return;
    }
    const GlApi& api = rig.gl;
    Report report(problems);
    int tested = 0;
    for (const FormatEntry& entry : FormatTable::all()) {
        if (!selected(entry, packed) || entry.gl_internal == 0) {
            continue;
        }
        if (!GlFormatIo::reported_supported(api, entry.gl_internal)) {
            continue;
        }
        const FormatSpec spec = FormatData::spec_of(entry.dxgi);
        const std::string name = std::string("OpenGL ") + entry.name;
        const std::vector<std::uint8_t> data = FormatData::make(entry.dxgi, 0x3000 + static_cast<std::uint64_t>(entry.dxgi), kWidth, kHeight);
        std::vector<std::uint8_t> back;
        const GlFormatIo::Result result = GlFormatIo::upload_read(api, entry, static_cast<int>(kWidth), static_cast<int>(kHeight), data, back, error);
        if (result == GlFormatIo::Result::Unsupported) {
            const bool needs_extension = entry.dxgi == DXGI_FORMAT_B4G4R4A4_UNORM || entry.dxgi == DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
            if (!needs_extension || api.at_least(4, 2)) {
                report.add(name + " is reported as supported but the upload failed: " + error);
            }
            continue;
        }
        ++tested;
        if (result == GlFormatIo::Result::Failed) {
            report.add(name + ": " + error);
        } else if (back != data) {
            report.add(name + " upload and readback changed byte " + std::to_string(Kit::first_difference(data, back)) + " of " +
                       std::to_string(data.size()));
            dump(context, std::string("fmt.gl.") + entry.name, data, back);
        }
        if (!FormatCodec::spec(entry.dxgi).known || spec.kind == FormatKind::Srgb) {
            continue;
        }
        const RenderCase c = render_case(spec);
        const GlFormatIo::Result cleared = GlFormatIo::clear_read(api, entry, spec, c.values, 8, 8, back, error);
        std::string why;
        if (cleared == GlFormatIo::Result::Failed) {
            report.add(name + ": " + error);
        } else if (cleared == GlFormatIo::Result::Done &&
                   !FormatCodec::matches(spec, back.data() + static_cast<std::size_t>(spec.bytes) * 9, c.values, why)) {
            report.add(name + " cleared to the wrong value: " + why);
        }
    }
    context.measure("fmt.roundtrip.gl.formats", tested);
    static_cast<void>(skips);
}

Outcome opencl_part(Context& context) {
    return cl::Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        if (!rig.images()) {
            return;
        }
        std::vector<cl_image_format> formats;
        cl_uint count = 0;
        cl_int code = rig.api.GetSupportedImageFormats(rig.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, 0, nullptr, &count);
        if (code != CL_SUCCESS) {
            problems.add("clGetSupportedImageFormats failed: " + ClTools::error_name(code));
            return;
        }
        formats.assign(count, cl_image_format{});
        if (count != 0) {
            code = rig.api.GetSupportedImageFormats(rig.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, count, formats.data(), nullptr);
        }
        Report report(problems);
        int tested = 0;
        for (const cl_image_format& format : formats) {
            const std::size_t element = ClImageFormats::element_size(format);
            if (element == 0 || !ClImageFormats::known(format)) {
                continue;
            }
            ++tested;
            const std::string name = "OpenCL " + ClImageFormats::name(format);
            std::vector<std::uint8_t> data = cl::Kit::bytes(0x4000 + static_cast<std::uint64_t>(format.image_channel_order) * 64 + format.image_channel_data_type,
                                                              element * kWidth * kHeight);
            ClImageFormats::sanitize(format, data.data(), data.size());
            cl::ImageShape shape;
            shape.type = CL_MEM_OBJECT_IMAGE2D;
            shape.width = kWidth;
            shape.height = kHeight;
            ClOwned<cl_mem> image = cl::ImageKit::create(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, format, shape, data.data(), code);
            if (!image) {
                report.add(name + " is listed as supported but image creation failed with " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> back(data.size(), 0xCD);
            const std::size_t origin[3] = {0, 0, 0};
            const std::size_t region[3] = {kWidth, kHeight, 1};
            code = rig.api.EnqueueReadImage(rig.queue, image.get(), CL_TRUE, origin, region, 0, 0, back.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                report.add(name + " readback failed with " + ClTools::error_name(code));
            } else if (back != data) {
                report.add(name + " creation from host memory and readback changed byte " + std::to_string(Kit::first_difference(data, back)));
                dump(context, "fmt.cl." + cl::Kit::sanitize(name), data, back);
            }
        }
        context.measure("fmt.roundtrip.opencl.formats", tested);
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

Outcome run_matrix(Context& context, bool packed) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    const auto part = [&](const char* api, auto&& body) {
        if (!Kit::enabled(api)) {
            skips.push_back(std::string(api) + " excluded");
            return;
        }
        body();
    };
    part("d3d11", [&] {
        d3d11_part(context, packed, problems);
        exercised = true;
    });
    part("d3d12", [&] {
        d3d12_part(context, packed, problems);
        exercised = true;
    });
    part("vulkan", [&] { merge("Vulkan", vulkan_part(context, packed), problems, skips, exercised); });
    part("gl", [&] {
        gl_part(context, packed, problems, skips);
        exercised = true;
    });
    if (!packed) {
        part("opencl", [&] { merge("OpenCL", opencl_part(context), problems, skips, exercised); });
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace

Outcome RoundtripChecks::roundtrip_uncompressed(Context& context) {
    return run_matrix(context, false);
}

Outcome RoundtripChecks::packed_formats(Context& context) {
    return run_matrix(context, true);
}

}  // namespace lsw::suites::sanity::fmt
