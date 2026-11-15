#include "sanity/cl/image_checks.hpp"

#include "sanity/cl/image_kit.hpp"
#include "support/cl_ulp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace lsw::suites::sanity::cl {

namespace {

bool has_format(const std::vector<cl_image_format>& list, const cl_image_format& wanted) {
    return std::any_of(list.begin(), list.end(), [&](const cl_image_format& f) {
        return f.image_channel_order == wanted.image_channel_order && f.image_channel_data_type == wanted.image_channel_data_type;
    });
}

bool list_formats(ClRig& rig, cl_mem_flags flags, cl_mem_object_type type, std::vector<cl_image_format>& out, cl_int& code) {
    cl_uint count = 0;
    code = rig.api.GetSupportedImageFormats(rig.context, flags, type, 0, nullptr, &count);
    if (code != CL_SUCCESS) {
        return false;
    }
    out.assign(count, cl_image_format{});
    if (count == 0) {
        return true;
    }
    code = rig.api.GetSupportedImageFormats(rig.context, flags, type, count, out.data(), nullptr);
    return code == CL_SUCCESS;
}

bool normalized_conversion(cl_channel_type type) {
    return type == CL_UNORM_INT8 || type == CL_UNORM_INT16 || type == CL_SNORM_INT8 || type == CL_SNORM_INT16 ||
           type == CL_UNORM_SHORT_565 || type == CL_UNORM_SHORT_555 || type == CL_UNORM_INT_101010;
}

struct Held {
    ClOwned<cl_mem> image;
    ClOwned<cl_mem> backing;
    ClOwned<cl_mem> output;
    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> readback;
    cl_image_format format{};
    ImageShape shape;
    cl_mem_flags flags = 0;
    bool kernel = false;
    ClKernel reader;
};

void info_consistency(ClRig& rig, cl_mem image, const ImageShape& shape, const cl_image_format& format, Problems& problems,
                      const std::string& tag) {
    auto size_info = [&](cl_image_info param) {
        std::size_t value = ~std::size_t(0);
        rig.api.GetImageInfo(image, param, sizeof(value), &value, nullptr);
        return value;
    };
    const std::size_t want_h = shape.type == CL_MEM_OBJECT_IMAGE1D || shape.type == CL_MEM_OBJECT_IMAGE1D_BUFFER ||
                                       shape.type == CL_MEM_OBJECT_IMAGE1D_ARRAY
                                   ? 0
                                   : shape.height;
    const std::size_t want_d = shape.type == CL_MEM_OBJECT_IMAGE3D ? shape.depth : 0;
    if (size_info(CL_IMAGE_WIDTH) != shape.width) {
        problems.add(tag + ": CL_IMAGE_WIDTH is " + std::to_string(size_info(CL_IMAGE_WIDTH)) + ", created with " + std::to_string(shape.width));
    }
    if (size_info(CL_IMAGE_HEIGHT) != want_h) {
        problems.add(tag + ": CL_IMAGE_HEIGHT is " + std::to_string(size_info(CL_IMAGE_HEIGHT)) + ", expected " + std::to_string(want_h));
    }
    if (size_info(CL_IMAGE_DEPTH) != want_d) {
        problems.add(tag + ": CL_IMAGE_DEPTH is " + std::to_string(size_info(CL_IMAGE_DEPTH)) + ", expected " + std::to_string(want_d));
    }
    if (size_info(CL_IMAGE_ARRAY_SIZE) != shape.array) {
        problems.add(tag + ": CL_IMAGE_ARRAY_SIZE is " + std::to_string(size_info(CL_IMAGE_ARRAY_SIZE)) + ", expected " + std::to_string(shape.array));
    }
    if (size_info(CL_IMAGE_ELEMENT_SIZE) != ClImageFormats::element_size(format)) {
        problems.add(tag + ": CL_IMAGE_ELEMENT_SIZE is " + std::to_string(size_info(CL_IMAGE_ELEMENT_SIZE)) + ", expected " + std::to_string(ClImageFormats::element_size(format)));
    }
    if (size_info(CL_IMAGE_ROW_PITCH) < shape.width * ClImageFormats::element_size(format)) {
        problems.add(tag + ": CL_IMAGE_ROW_PITCH is below width x element size");
    }
    cl_image_format reported{};
    rig.api.GetImageInfo(image, CL_IMAGE_FORMAT, sizeof(reported), &reported, nullptr);
    if (reported.image_channel_order != format.image_channel_order || reported.image_channel_data_type != format.image_channel_data_type) {
        problems.add(tag + ": CL_IMAGE_FORMAT reads back as " + ClImageFormats::name(reported) + ", created as " + ClImageFormats::name(format));
    }
    cl_mem_object_type type = 0;
    rig.api.GetMemObjectInfo(image, CL_MEM_TYPE, sizeof(type), &type, nullptr);
    if (type != shape.type) {
        problems.add(tag + ": CL_MEM_TYPE is 0x" + Text::hex(type, 4) + ", created as 0x" + Text::hex(shape.type, 4));
    }
}

}  // namespace

Outcome ImageChecks::formats_supported(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        context.measure("cl.sizeof_image_desc", sizeof(cl_image_desc));
        context.measure("cl.sizeof_image_format", sizeof(cl_image_format));
        if (!rig.images()) {
            if (rig.version.at_least(3, 0) == false && rig.profile_full()) {
                return Outcome::fail("CL_DEVICE_IMAGE_SUPPORT is CL_FALSE on a FULL_PROFILE device of version " + rig.version.text());
            }
            const cl_image_format format{CL_RGBA, CL_UNORM_INT8};
            const cl_image_desc desc = ImageKit::descriptor({CL_MEM_OBJECT_IMAGE2D, 4, 4, 0, 0});
            cl_int code = CL_SUCCESS;
            cl_mem image = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &desc, nullptr, &code);
            if (image != nullptr) {
                rig.api.ReleaseMemObject(image);
                return Outcome::fail("CL_DEVICE_IMAGE_SUPPORT is CL_FALSE yet clCreateImage succeeded");
            }
            Kit::expect(problems, "clCreateImage on a device without image support", code, {CL_INVALID_OPERATION});
            cl_uint count = 0;
            Kit::expect(problems, "clGetSupportedImageFormats on a device without image support", rig.api.GetSupportedImageFormats(rig.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, 0, nullptr, &count), {CL_INVALID_OPERATION});
            return problems.outcome();
        }
        const std::vector<ImageShape> shapes = ImageKit::shapes(rig, true);
        const cl_image_format basic{CL_RGBA, CL_UNORM_INT8};
        for (const ImageShape& shape : shapes) {
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> backing;
            ClOwned<cl_mem> image = ImageKit::create(rig, CL_MEM_READ_WRITE, basic, shape, nullptr, code, &backing);
            if (!image) {
                problems.add("clCreateImage(" + shape.name() + ", " + ClImageFormats::name(basic) + ", cl_image_desc of " + std::to_string(sizeof(cl_image_desc)) + " bytes) returned " + ClTools::error_name(code));
                continue;
            }
            info_consistency(rig, image.get(), shape, basic, problems, shape.name() + " descriptor");
        }
        ClProgram readers;
        bool readers_ready = false;
        if (rig.version.at_least(1, 2)) {
            std::string error;
            readers_ready = readers.build(rig, ImageKit::kernel_read_source(shapes), "-cl-std=CL1.2", error);
            if (!readers_ready) {
                problems.add("image read kernels: " + error);
            }
        }
        const struct {
            cl_mem_flags flags;
            const char* name;
        } lists[] = {{CL_MEM_READ_ONLY, "READ_ONLY"}, {CL_MEM_WRITE_ONLY, "WRITE_ONLY"}, {CL_MEM_READ_WRITE, "READ_WRITE"}};
        std::size_t tested = 0;
        std::size_t kernel_checked = 0;
        std::size_t untested = 0;
        for (const ImageShape& shape : shapes) {
            for (const auto& list : lists) {
                const std::string tag = shape.name() + " " + list.name;
                std::vector<cl_image_format> formats;
                cl_int code = CL_SUCCESS;
                if (!list_formats(rig, list.flags, shape.type, formats, code)) {
                    problems.add(tag + ": clGetSupportedImageFormats returned " + ClTools::error_name(code));
                    continue;
                }
                const bool required_list = shape.type != CL_MEM_OBJECT_IMAGE1D_BUFFER &&
                                           (list.flags == CL_MEM_READ_ONLY || (list.flags == CL_MEM_WRITE_ONLY && shape.type == CL_MEM_OBJECT_IMAGE2D));
                if (required_list) {
                    for (const cl_image_format& wanted : ClImageFormats::required_read_only()) {
                        if (!has_format(formats, wanted)) {
                            problems.add(tag + ": required format " + ClImageFormats::name(wanted) + " is not listed");
                        }
                    }
                }
                cl_uint read_write_args = rig.uint_info(CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS);
                if (list.flags == CL_MEM_READ_WRITE && shape.type == CL_MEM_OBJECT_IMAGE2D && rig.version.at_least(2, 0) &&
                    (!rig.version.at_least(3, 0) || read_write_args > 0)) {
                    for (const cl_image_format& wanted : ClImageFormats::required_read_write()) {
                        if (!has_format(formats, wanted)) {
                            problems.add(tag + ": required format " + ClImageFormats::name(wanted) + " is not listed");
                        }
                    }
                }
                std::vector<Held> held;
                held.reserve(formats.size());
                for (const cl_image_format& format : formats) {
                    if (!ClImageFormats::known(format)) {
                        ++untested;
                        continue;
                    }
                    Held item;
                    item.format = format;
                    item.shape = shape;
                    item.flags = list.flags;
                    item.image = ImageKit::create(rig, list.flags, format, shape, nullptr, code, &item.backing);
                    if (!item.image) {
                        problems.add(tag + ": listed format " + ClImageFormats::name(format) + " cannot be created: " + ClTools::error_name(code));
                        continue;
                    }
                    info_consistency(rig, item.image.get(), shape, format, problems, tag + " " + ClImageFormats::name(format));
                    const std::size_t element = ClImageFormats::element_size(format);
                    item.input = Kit::bytes(900 + tested, shape.pixels() * element);
                    ClImageFormats::sanitize(format, item.input.data(), item.input.size());
                    item.readback.assign(item.input.size(), 0xCD);
                    const auto region = shape.region();
                    const std::size_t origin[3] = {0, 0, 0};
                    code = rig.api.EnqueueWriteImage(rig.queue, item.image.get(), CL_FALSE, origin, region.data(), 0, 0, item.input.data(), 0, nullptr, nullptr);
                    if (code == CL_SUCCESS) {
                        code = rig.api.EnqueueReadImage(rig.queue, item.image.get(), CL_FALSE, origin, region.data(), 0, 0, item.readback.data(), 0, nullptr, nullptr);
                    }
                    if (code != CL_SUCCESS) {
                        problems.add(tag + " " + ClImageFormats::name(format) + ": write or read returned " + ClTools::error_name(code));
                        rig.api.Finish(rig.queue);
                        item.image.reset();
                        continue;
                    }
                    item.kernel = readers_ready && list.flags != CL_MEM_WRITE_ONLY && ClImageFormats::kernel_readable(format);
                    if (item.kernel) {
                        const ClReadClass read_class = ClImageFormats::read_class(format.image_channel_data_type);
                        std::string error;
                        const std::string name = ImageKit::read_kernel_name(shape, read_class);
                        item.reader = readers.kernel(name.c_str(), error);
                        item.output = Kit::buffer(rig, CL_MEM_READ_WRITE, shape.pixels() * 16, nullptr, code);
                        if (!item.reader || !item.output || item.reader.set(item.image, item.output) != CL_SUCCESS) {
                            problems.add(tag + " " + ClImageFormats::name(format) + ": preparing the read kernel failed " + error);
                            item.kernel = false;
                        } else {
                            const std::size_t global[3] = {region[0], region[1], region[2]};
                            const cl_uint dims = shape.type == CL_MEM_OBJECT_IMAGE1D || shape.type == CL_MEM_OBJECT_IMAGE1D_BUFFER ? 1u : (region[2] > 1 ? 3u : 2u);
                            code = item.reader.enqueue(rig.queue, dims, nullptr, global, nullptr);
                            if (code != CL_SUCCESS) {
                                problems.add(tag + " " + ClImageFormats::name(format) + ": the read kernel returned " + ClTools::error_name(code));
                                item.kernel = false;
                            }
                        }
                    }
                    held.push_back(std::move(item));
                    ++tested;
                }
                code = rig.api.Finish(rig.queue);
                Kit::expect(problems, tag + ": clFinish", code, {CL_SUCCESS});
                for (Held& item : held) {
                    const std::string name = tag + " " + ClImageFormats::name(item.format);
                    Kit::same_bytes(context, problems, name + " write/read round trip", item.input.data(), item.readback.data(), item.input.size());
                    if (!item.kernel) {
                        continue;
                    }
                    ++kernel_checked;
                    const std::size_t pixels = item.shape.pixels();
                    const std::size_t element = ClImageFormats::element_size(item.format);
                    std::vector<std::uint8_t> raw(pixels * 16);
                    if (Kit::read(rig, item.output.get(), raw.data(), raw.size()) != CL_SUCCESS) {
                        problems.add(name + ": reading the kernel output failed");
                        continue;
                    }
                    const ClReadClass read_class = ClImageFormats::read_class(item.format.image_channel_data_type);
                    std::size_t bad = 0;
                    std::string first;
                    for (std::size_t p = 0; p < pixels && bad < 3; ++p) {
                        ClTexel texel;
                        if (!ClImageFormats::decode(item.format, item.input.data() + p * element, texel)) {
                            continue;
                        }
                        for (int c = 0; c < 4; ++c) {
                            const std::uint8_t* at = raw.data() + p * 16 + static_cast<std::size_t>(c) * 4;
                            bool same = true;
                            std::string got;
                            std::string want;
                            if (read_class == ClReadClass::Float) {
                                float value;
                                std::memcpy(&value, at, 4);
                                const double limit = normalized_conversion(item.format.image_channel_data_type) ? 2.0 : 0.0;
                                const double err = ClUlp::error(static_cast<long double>(texel.value[c]), value);
                                same = err <= limit && !std::isnan(value);
                                got = std::to_string(value);
                                want = std::to_string(texel.value[c]);
                            } else if (read_class == ClReadClass::Signed) {
                                std::int32_t value;
                                std::memcpy(&value, at, 4);
                                same = value == texel.integer[c];
                                got = std::to_string(value);
                                want = std::to_string(texel.integer[c]);
                            } else {
                                std::uint32_t value;
                                std::memcpy(&value, at, 4);
                                same = static_cast<std::int64_t>(value) == texel.integer[c];
                                got = std::to_string(value);
                                want = std::to_string(texel.integer[c]);
                            }
                            if (!same) {
                                ++bad;
                                if (first.empty()) {
                                    first = "pixel " + std::to_string(p) + " channel " + std::to_string(c) + " read " + got + ", expected " + want;
                                }
                                break;
                            }
                        }
                    }
                    if (bad != 0) {
                        problems.add(name + " read in a kernel: " + first);
                    }
                }
            }
        }
        context.measure("cl.formats_round_tripped", tested);
        context.measure("cl.formats_not_modelled", untested);
        context.measure("cl.formats_read_in_kernel", kernel_checked);
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
