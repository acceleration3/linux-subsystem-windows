#include "sanity/cl/image_kernel_checks.hpp"

#include "sanity/cl/image_kit.hpp"
#include "support/cl_ulp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::cl {

namespace {

constexpr int kSide = 8;

struct Combo {
    cl_bool normalized;
    cl_addressing_mode address;
    cl_filter_mode filter;
    const char* address_flag;
    const char* filter_flag;

    [[nodiscard]] std::string name() const {
        return std::string(normalized != CL_FALSE ? "normalized" : "unnormalized") + " " + address_flag + " " + filter_flag;
    }
};

std::vector<Combo> combos() {
    const struct {
        cl_addressing_mode mode;
        const char* flag;
        bool unnormalized_ok;
    } addresses[] = {{CL_ADDRESS_NONE, "CLK_ADDRESS_NONE", true},
                     {CL_ADDRESS_CLAMP, "CLK_ADDRESS_CLAMP", true},
                     {CL_ADDRESS_CLAMP_TO_EDGE, "CLK_ADDRESS_CLAMP_TO_EDGE", true},
                     {CL_ADDRESS_REPEAT, "CLK_ADDRESS_REPEAT", false},
                     {CL_ADDRESS_MIRRORED_REPEAT, "CLK_ADDRESS_MIRRORED_REPEAT", false}};
    std::vector<Combo> list;
    for (const cl_bool normalized : std::initializer_list<cl_bool>{CL_FALSE, CL_TRUE}) {
        for (const auto& address : addresses) {
            if (normalized == CL_FALSE && !address.unnormalized_ok) {
                continue;
            }
            list.push_back({normalized, address.mode, CL_FILTER_NEAREST, address.flag, "CLK_FILTER_NEAREST"});
            list.push_back({normalized, address.mode, CL_FILTER_LINEAR, address.flag, "CLK_FILTER_LINEAR"});
        }
    }
    return list;
}

int map_index(long index, cl_addressing_mode mode) {
    const long n = kSide;
    switch (mode) {
    case CL_ADDRESS_CLAMP_TO_EDGE:
        return static_cast<int>(std::clamp(index, 0L, n - 1));
    case CL_ADDRESS_CLAMP:
    case CL_ADDRESS_NONE:
        return index < 0 || index >= n ? -1 : static_cast<int>(index);
    case CL_ADDRESS_REPEAT:
        return static_cast<int>(((index % n) + n) % n);
    default: {
        const long m = ((index % (2 * n)) + 2 * n) % (2 * n);
        return static_cast<int>(m < n ? m : 2 * n - 1 - m);
    }
    }
}

struct Taps {
    int index[2] = {-1, -1};
    double weight[2] = {0, 0};
};

Taps taps_1d(double u, const Combo& combo) {
    Taps taps;
    if (combo.filter == CL_FILTER_NEAREST) {
        taps.index[0] = map_index(static_cast<long>(std::floor(u)), combo.address);
        taps.weight[0] = 1.0;
        return taps;
    }
    const double shifted = u - 0.5;
    const long i0 = static_cast<long>(std::floor(shifted));
    const double a = shifted - static_cast<double>(i0);
    taps.index[0] = map_index(i0, combo.address);
    taps.index[1] = map_index(i0 + 1, combo.address);
    taps.weight[0] = 1.0 - a;
    taps.weight[1] = a;
    return taps;
}

struct Expected {
    double value[4] = {0, 0, 0, 0};
    double spread = 0;
};

Expected model_float(const std::vector<std::uint8_t>& image, double u, double v, const Combo& combo) {
    const Taps tx = taps_1d(u, combo);
    const Taps ty = taps_1d(v, combo);
    Expected out;
    double low[4] = {1e9, 1e9, 1e9, 1e9};
    double high[4] = {-1e9, -1e9, -1e9, -1e9};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const double weight = tx.weight[i] * ty.weight[j];
            if (weight == 0.0) {
                continue;
            }
            for (int c = 0; c < 4; ++c) {
                const double texel = tx.index[i] < 0 || ty.index[j] < 0 ? 0.0 : static_cast<double>(image[(static_cast<std::size_t>(ty.index[j]) * kSide + static_cast<std::size_t>(tx.index[i])) * 4 + static_cast<std::size_t>(c)]) / 255.0;
                out.value[c] += weight * texel;
                low[c] = std::min(low[c], texel);
                high[c] = std::max(high[c], texel);
            }
        }
    }
    for (int c = 0; c < 4; ++c) {
        out.spread = std::max(out.spread, high[c] - low[c]);
    }
    return out;
}

std::vector<std::array<float, 2>> coordinates(const Combo& combo) {
    const double xs[] = {0.5, 1.5, 3.5, 4.0, 7.5, -0.5, -1.5, 8.5, 9.0, 12.5, 15.5, -8.5};
    const double ys[] = {0.5, 4.0, 7.5, -0.5, 8.5, 11.5};
    std::vector<std::array<float, 2>> list;
    for (const double y : ys) {
        for (const double x : xs) {
            const bool inside = x >= 0 && x < kSide && y >= 0 && y < kSide;
            if (combo.address == CL_ADDRESS_NONE && !inside) {
                continue;
            }
            const double scale = combo.normalized != CL_FALSE ? 1.0 / kSide : 1.0;
            list.push_back({static_cast<float>(x * scale), static_cast<float>(y * scale)});
        }
    }
    return list;
}

std::string kernel_source(const std::vector<Combo>& all) {
    std::string source =
        "__kernel void samp_f(__read_only image2d_t img, sampler_t s, __global const float2* c, __global float4* out) {\n"
        "    size_t i = get_global_id(0); out[i] = read_imagef(img, s, c[i]);\n}\n"
        "__kernel void samp_i(__read_only image2d_t img, sampler_t s, __global const float2* c, __global int4* out) {\n"
        "    size_t i = get_global_id(0); out[i] = read_imagei(img, s, c[i]);\n}\n"
        "__kernel void samp_ui(__read_only image2d_t img, sampler_t s, __global const float2* c, __global uint4* out) {\n"
        "    size_t i = get_global_id(0); out[i] = read_imageui(img, s, c[i]);\n}\n"
        "__kernel void wr_f(__write_only image2d_t img, __global const uchar4* in) {\n"
        "    int x = (int)get_global_id(0), y = (int)get_global_id(1);\n"
        "    write_imagef(img, (int2)(x, y), convert_float4(in[x + y * 8]) / 255.0f);\n}\n"
        "__kernel void wr_ui(__write_only image2d_t img, __global const uchar4* in) {\n"
        "    int x = (int)get_global_id(0), y = (int)get_global_id(1);\n"
        "    write_imageui(img, (int2)(x, y), convert_uint4(in[x + y * 8]));\n}\n"
        "__kernel void wr_i(__write_only image2d_t img, __global const uchar4* in) {\n"
        "    int x = (int)get_global_id(0), y = (int)get_global_id(1);\n"
        "    write_imagei(img, (int2)(x, y), convert_int4(as_char4(in[x + y * 8])));\n}\n";
    for (std::size_t k = 0; k < all.size(); ++k) {
        const Combo& c = all[k];
        source += "__kernel void inl_" + std::to_string(k) + "(__read_only image2d_t img, __global const float2* c, __global float4* out) {\n" +
                  "    const sampler_t s = " + (c.normalized != CL_FALSE ? "CLK_NORMALIZED_COORDS_TRUE" : "CLK_NORMALIZED_COORDS_FALSE") + " | " +
                  c.address_flag + " | " + c.filter_flag + ";\n" +
                  "    size_t i = get_global_id(0); out[i] = read_imagef(img, s, c[i]);\n}\n";
    }
    return source;
}

cl_sampler make_sampler(ClRig& rig, const Combo& combo, cl_int& code) {
    if (rig.version.at_least(2, 0) && rig.api.CreateSamplerWithProperties != nullptr) {
        const cl_sampler_properties properties[] = {CL_SAMPLER_NORMALIZED_COORDS, combo.normalized, CL_SAMPLER_ADDRESSING_MODE, combo.address,
                                                    CL_SAMPLER_FILTER_MODE, combo.filter, 0};
        return rig.api.CreateSamplerWithProperties(rig.context, properties, &code);
    }
    return rig.api.CreateSampler(rig.context, combo.normalized, combo.address, combo.filter, &code);
}

}  // namespace

Outcome ImageKernelChecks::images_in_kernels(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.images()) {
            return Context::skip("CL_DEVICE_IMAGE_SUPPORT is CL_FALSE");
        }
        const std::vector<Combo> all = combos();
        ClProgram program;
        if (!Kit::compile(rig, program, kernel_source(all), rig.version.at_least(1, 2) ? "-cl-std=CL1.2" : "", problems, "sampler kernels")) {
            return problems.outcome();
        }
        cl_int code = CL_SUCCESS;
        const std::size_t zero[3] = {0, 0, 0};
        const std::size_t whole[3] = {kSide, kSide, 1};
        const std::vector<std::uint8_t> pixels = Kit::bytes(1200, kSide * kSide * 4);
        const cl_image_format formats[3] = {{CL_RGBA, CL_UNORM_INT8}, {CL_RGBA, CL_SIGNED_INT8}, {CL_RGBA, CL_UNSIGNED_INT8}};
        ClOwned<cl_mem> images[3];
        for (int f = 0; f < 3; ++f) {
            images[f] = ImageKit::create(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, formats[f], {CL_MEM_OBJECT_IMAGE2D, kSide, kSide, 0, 0}, const_cast<std::uint8_t*>(pixels.data()), code);
            if (!images[f]) {
                problems.add(ClImageFormats::name(formats[f]) + " 2D image: " + ClTools::error_name(code));
                return problems.outcome();
            }
        }
        for (std::size_t k = 0; k < all.size(); ++k) {
            const Combo& combo = all[k];
            const std::vector<std::array<float, 2>> coords = coordinates(combo);
            const std::size_t count = coords.size();
            ClOwned<cl_mem> coord_buffer = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 8, const_cast<std::array<float, 2>*>(coords.data()), code);
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, count * 16, nullptr, code);
            ClOwned<cl_sampler> sampler(rig, make_sampler(rig, combo, code));
            if (!coord_buffer || !out || !sampler) {
                problems.add(combo.name() + ": creating the sampler or buffers returned " + ClTools::error_name(code));
                continue;
            }
            cl_bool normalized = 2;
            cl_addressing_mode address = 0;
            cl_filter_mode filter = 0;
            rig.api.GetSamplerInfo(sampler.get(), CL_SAMPLER_NORMALIZED_COORDS, sizeof(normalized), &normalized, nullptr);
            rig.api.GetSamplerInfo(sampler.get(), CL_SAMPLER_ADDRESSING_MODE, sizeof(address), &address, nullptr);
            rig.api.GetSamplerInfo(sampler.get(), CL_SAMPLER_FILTER_MODE, sizeof(filter), &filter, nullptr);
            if (normalized != combo.normalized || address != combo.address || filter != combo.filter) {
                problems.add(combo.name() + ": clGetSamplerInfo does not return the creation arguments");
            }
            auto check_float = [&](const char* kernel_name, bool inline_sampler) {
                std::string error;
                ClKernel kernel = program.kernel(kernel_name, error);
                if (!kernel) {
                    problems.add(combo.name() + ": " + error);
                    return;
                }
                cl_int set = inline_sampler ? kernel.set(images[0], coord_buffer, out) : kernel.set(images[0], sampler.get(), coord_buffer, out);
                if (set == CL_SUCCESS) {
                    set = kernel.run(count);
                }
                std::vector<float> got(count * 4);
                if (set == CL_SUCCESS) {
                    set = Kit::read(rig, out.get(), got.data(), got.size() * 4);
                }
                if (set != CL_SUCCESS) {
                    problems.add(combo.name() + " (" + (inline_sampler ? "inline" : "host") + " sampler): returned " + ClTools::error_name(set));
                    return;
                }
                std::size_t bad = 0;
                for (std::size_t i = 0; i < count && bad < 2; ++i) {
                    const double scale = combo.normalized != CL_FALSE ? kSide : 1.0;
                    const Expected want = model_float(pixels, static_cast<double>(coords[i][0]) * scale, static_cast<double>(coords[i][1]) * scale, combo);
                    for (int c = 0; c < 4; ++c) {
                        const double actual = static_cast<double>(got[i * 4 + static_cast<std::size_t>(c)]);
                        bool good;
                        if (combo.filter == CL_FILTER_NEAREST) {
                            good = ClUlp::error(static_cast<long double>(want.value[c]), got[i * 4 + static_cast<std::size_t>(c)]) <= 2.0;
                        } else {
                            good = std::fabs(actual - want.value[c]) <= want.spread * (2.0 / 256.0) + 1e-6;
                        }
                        if (!good || std::isnan(actual)) {
                            ++bad;
                            problems.add(combo.name() + " (" + (inline_sampler ? "inline" : "host") + " sampler) coordinate (" + std::to_string(coords[i][0]) + ", " + std::to_string(coords[i][1]) + ") channel " + std::to_string(c) + " read " + std::to_string(actual) + ", model " + std::to_string(want.value[c]));
                            break;
                        }
                    }
                }
            };
            check_float("samp_f", false);
            check_float(("inl_" + std::to_string(k)).c_str(), true);
            if (combo.filter == CL_FILTER_NEAREST) {
                for (int integer = 0; integer < 2; ++integer) {
                    std::string error;
                    ClKernel kernel = program.kernel(integer == 0 ? "samp_i" : "samp_ui", error);
                    if (!kernel) {
                        problems.add(combo.name() + ": " + error);
                        continue;
                    }
                    cl_int set = kernel.set(images[integer == 0 ? 1 : 2], sampler.get(), coord_buffer, out);
                    if (set == CL_SUCCESS) {
                        set = kernel.run(count);
                    }
                    std::vector<std::int32_t> got(count * 4);
                    if (set == CL_SUCCESS) {
                        set = Kit::read(rig, out.get(), got.data(), got.size() * 4);
                    }
                    if (set != CL_SUCCESS) {
                        problems.add(combo.name() + " integer read returned " + ClTools::error_name(set));
                        continue;
                    }
                    for (std::size_t i = 0; i < count; ++i) {
                        const double scale = combo.normalized != CL_FALSE ? kSide : 1.0;
                        const Taps tx = taps_1d(static_cast<double>(coords[i][0]) * scale, combo);
                        const Taps ty = taps_1d(static_cast<double>(coords[i][1]) * scale, combo);
                        bool good = true;
                        for (int c = 0; c < 4 && good; ++c) {
                            std::int32_t want = 0;
                            if (tx.index[0] >= 0 && ty.index[0] >= 0) {
                                const std::uint8_t raw = pixels[(static_cast<std::size_t>(ty.index[0]) * kSide + static_cast<std::size_t>(tx.index[0])) * 4 + static_cast<std::size_t>(c)];
                                want = integer == 0 ? static_cast<std::int32_t>(static_cast<std::int8_t>(raw)) : static_cast<std::int32_t>(raw);
                            }
                            good = got[i * 4 + static_cast<std::size_t>(c)] == want;
                            if (!good) {
                                problems.add(combo.name() + " read_image" + (integer == 0 ? "i" : "ui") + " at (" + std::to_string(coords[i][0]) + ", " + std::to_string(coords[i][1]) + ") channel " + std::to_string(c) + " read " + std::to_string(got[i * 4 + static_cast<std::size_t>(c)]) + ", expected " + std::to_string(want));
                            }
                        }
                        if (!good) {
                            break;
                        }
                    }
                }
            }
        }
        {
            const Combo invalid_unnormalized_repeat{CL_FALSE, CL_ADDRESS_REPEAT, CL_FILTER_NEAREST, "CLK_ADDRESS_REPEAT", "CLK_FILTER_NEAREST"};
            cl_int result = CL_SUCCESS;
            cl_sampler sampler = make_sampler(rig, invalid_unnormalized_repeat, result);
            context.measure("cl.sampler_unnormalized_repeat", sampler != nullptr ? std::string("accepted") : ClTools::error_name(result));
            if (sampler != nullptr) {
                rig.api.ReleaseSampler(sampler);
            }
            const Combo bad_mode{CL_TRUE, 0x9999, CL_FILTER_NEAREST, "invalid", "CLK_FILTER_NEAREST"};
            sampler = make_sampler(rig, bad_mode, result);
            if (sampler != nullptr) {
                rig.api.ReleaseSampler(sampler);
                problems.add("a sampler with an undefined addressing mode was created");
            } else {
                Kit::expect(problems, "a sampler with an undefined addressing mode", result, {CL_INVALID_VALUE});
            }
            const Combo bad_filter{CL_TRUE, CL_ADDRESS_CLAMP, 0x9999, "CLK_ADDRESS_CLAMP", "invalid"};
            sampler = make_sampler(rig, bad_filter, result);
            if (sampler != nullptr) {
                rig.api.ReleaseSampler(sampler);
                problems.add("a sampler with an undefined filter mode was created");
            } else {
                Kit::expect(problems, "a sampler with an undefined filter mode", result, {CL_INVALID_VALUE});
            }
        }
        const struct {
            const char* kernel;
            cl_image_format format;
            bool unsigned_values;
        } writers[] = {{"wr_f", {CL_RGBA, CL_UNORM_INT8}, true}, {"wr_ui", {CL_RGBA, CL_UNSIGNED_INT8}, true}, {"wr_i", {CL_RGBA, CL_SIGNED_INT8}, false}};
        for (const auto& writer : writers) {
            ClOwned<cl_mem> target = ImageKit::create(rig, CL_MEM_WRITE_ONLY, writer.format, {CL_MEM_OBJECT_IMAGE2D, kSide, kSide, 0, 0}, nullptr, code);
            ClOwned<cl_mem> input = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, pixels.size(), const_cast<std::uint8_t*>(pixels.data()), code);
            std::string error;
            ClKernel kernel = program.kernel(writer.kernel, error);
            if (!target || !input || !kernel) {
                problems.add(std::string(writer.kernel) + ": preparing returned " + ClTools::error_name(code) + " " + error);
                continue;
            }
            const std::size_t global[2] = {kSide, kSide};
            cl_int set = kernel.set(target, input);
            if (set == CL_SUCCESS) {
                set = kernel.enqueue(rig.queue, 2, nullptr, global, nullptr);
            }
            std::vector<std::uint8_t> got(pixels.size(), 0);
            if (set == CL_SUCCESS) {
                set = rig.api.EnqueueReadImage(rig.queue, target.get(), CL_TRUE, zero, whole, 0, 0, got.data(), 0, nullptr, nullptr);
            }
            if (set != CL_SUCCESS) {
                problems.add(std::string(writer.kernel) + " returned " + ClTools::error_name(set));
                continue;
            }
            Kit::same_bytes(context, problems, std::string("write_image through ") + writer.kernel + " into " + ClImageFormats::name(writer.format), pixels.data(), got.data(), pixels.size());
        }
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
