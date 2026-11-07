#include "sanity/cl/image_transfer_checks.hpp"

#include "sanity/cl/image_kit.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::cl {

namespace {

constexpr std::size_t kElement = 4;

struct Layout {
    ImageShape shape;
    std::array<std::size_t, 3> origin{};
    std::array<std::size_t, 3> destination{};
    std::array<std::size_t, 3> region{};
    std::size_t row_pitch = 0;
    std::size_t slice_pitch = 0;
};

std::size_t image_offset(const ImageShape& shape, std::size_t x, std::size_t y, std::size_t z) {
    const auto d = shape.dims();
    return ((z * d[1] + y) * d[0] + x) * kElement;
}

std::size_t host_offset(const Layout& l, std::size_t x, std::size_t y, std::size_t z) {
    const std::size_t row = l.row_pitch != 0 ? l.row_pitch : l.region[0] * kElement;
    switch (l.shape.type) {
    case CL_MEM_OBJECT_IMAGE1D:
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
        return x * kElement;
    case CL_MEM_OBJECT_IMAGE1D_ARRAY: {
        const std::size_t slice = l.slice_pitch != 0 ? l.slice_pitch : l.region[0] * kElement;
        return y * slice + x * kElement;
    }
    case CL_MEM_OBJECT_IMAGE2D:
        return y * row + x * kElement;
    default: {
        const std::size_t slice = l.slice_pitch != 0 ? l.slice_pitch : row * l.region[1];
        return z * slice + y * row + x * kElement;
    }
    }
}

std::vector<Layout> layouts(const ClRig& rig) {
    std::vector<Layout> list;
    auto add = [&](cl_mem_object_type type, std::size_t w, std::size_t h, std::size_t d, std::size_t arr, std::array<std::size_t, 3> origin,
                   std::array<std::size_t, 3> destination, std::array<std::size_t, 3> region, std::size_t row, std::size_t slice) {
        Layout l;
        l.shape = {type, w, h, d, arr};
        l.origin = origin;
        l.destination = destination;
        l.region = region;
        l.row_pitch = row;
        l.slice_pitch = slice;
        list.push_back(l);
    };
    if (rig.version.at_least(1, 2)) {
        add(CL_MEM_OBJECT_IMAGE1D, 24, 0, 0, 0, {3, 0, 0}, {11, 0, 0}, {9, 1, 1}, 0, 0);
        add(CL_MEM_OBJECT_IMAGE1D_ARRAY, 24, 0, 0, 5, {2, 1, 0}, {9, 2, 0}, {7, 2, 1}, 0, 0);
    }
    add(CL_MEM_OBJECT_IMAGE2D, 24, 12, 0, 0, {3, 2, 0}, {10, 5, 0}, {9, 5, 1}, 48, 0);
    if (rig.version.at_least(1, 2)) {
        add(CL_MEM_OBJECT_IMAGE2D_ARRAY, 24, 12, 0, 5, {1, 2, 1}, {12, 4, 2}, {7, 4, 2}, 36, 180);
    }
    add(CL_MEM_OBJECT_IMAGE3D, 24, 12, 5, 0, {2, 1, 1}, {13, 6, 2}, {6, 3, 2}, 28, 112);
    return list;
}

template <typename Visitor>
void each_texel(const std::array<std::size_t, 3>& region, Visitor&& visit) {
    for (std::size_t z = 0; z < region[2]; ++z) {
        for (std::size_t y = 0; y < region[1]; ++y) {
            for (std::size_t x = 0; x < region[0]; ++x) {
                visit(x, y, z);
            }
        }
    }
}

std::size_t host_size(const Layout& l) {
    std::size_t top = 0;
    each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) { top = std::max(top, host_offset(l, x, y, z) + kElement); });
    return top;
}

}  // namespace

Outcome ImageTransferChecks::transfers_rect_fill(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.images()) {
            return Context::skip("CL_DEVICE_IMAGE_SUPPORT is CL_FALSE");
        }
        const cl_image_format uint8{CL_RGBA, CL_UNSIGNED_INT8};
        const cl_image_format unorm8{CL_RGBA, CL_UNORM_INT8};
        std::uint64_t seed = 1000;
        for (const Layout& l : layouts(rig)) {
            const std::string tag = l.shape.name();
            cl_int code = CL_SUCCESS;
            const std::size_t bytes = l.shape.pixels() * kElement;
            const auto whole = l.shape.region();
            const std::size_t zero[3] = {0, 0, 0};
            ClOwned<cl_mem> a = ImageKit::create(rig, CL_MEM_READ_WRITE, uint8, l.shape, nullptr, code);
            ClOwned<cl_mem> b = ImageKit::create(rig, CL_MEM_READ_WRITE, uint8, l.shape, nullptr, code);
            ClOwned<cl_mem> other_format = ImageKit::create(rig, CL_MEM_READ_WRITE, unorm8, l.shape, nullptr, code);
            if (!a || !b || !other_format) {
                problems.add(tag + ": clCreateImage returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> model_a = Kit::bytes(++seed, bytes);
            std::vector<std::uint8_t> model_b = Kit::bytes(++seed, bytes);
            Kit::expect(problems, tag + ": initial write of A", rig.api.EnqueueWriteImage(rig.queue, a.get(), CL_TRUE, zero, whole.data(), 0, 0, model_a.data(), 0, nullptr, nullptr), {CL_SUCCESS});
            Kit::expect(problems, tag + ": initial write of B", rig.api.EnqueueWriteImage(rig.queue, b.get(), CL_TRUE, zero, whole.data(), 0, 0, model_b.data(), 0, nullptr, nullptr), {CL_SUCCESS});
            auto compare_image = [&](cl_mem image, const std::vector<std::uint8_t>& model, const std::string& what) {
                std::vector<std::uint8_t> actual(bytes, 0);
                const cl_int result = rig.api.EnqueueReadImage(rig.queue, image, CL_TRUE, zero, whole.data(), 0, 0, actual.data(), 0, nullptr, nullptr);
                if (result != CL_SUCCESS) {
                    problems.add(tag + " " + what + ": whole-image read returned " + ClTools::error_name(result));
                    return;
                }
                Kit::same_bytes(context, problems, tag + " " + what, model.data(), actual.data(), bytes);
            };
            compare_image(a.get(), model_a, "initial A");

            const std::size_t host_bytes = host_size(l) + 64;
            std::vector<std::uint8_t> host = Kit::bytes(++seed, host_bytes);
            code = rig.api.EnqueueWriteImage(rig.queue, a.get(), CL_TRUE, l.origin.data(), l.region.data(), l.row_pitch, l.slice_pitch, host.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": sub-region WriteImage with pitches returned " + ClTools::error_name(code));
            } else {
                each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                    std::memcpy(model_a.data() + image_offset(l.shape, l.origin[0] + x, l.origin[1] + y, l.origin[2] + z), host.data() + host_offset(l, x, y, z), kElement);
                });
                compare_image(a.get(), model_a, "WriteImage of a sub-region with host pitches");
            }

            std::vector<std::uint8_t> sink(host_bytes, 0x5A);
            code = rig.api.EnqueueReadImage(rig.queue, a.get(), CL_TRUE, l.origin.data(), l.region.data(), l.row_pitch, l.slice_pitch, sink.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": sub-region ReadImage with pitches returned " + ClTools::error_name(code));
            } else {
                std::vector<std::uint8_t> expected(host_bytes, 0x5A);
                each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                    std::memcpy(expected.data() + host_offset(l, x, y, z), model_a.data() + image_offset(l.shape, l.origin[0] + x, l.origin[1] + y, l.origin[2] + z), kElement);
                });
                Kit::same_bytes(context, problems, tag + " ReadImage of a sub-region with host pitches", expected.data(), sink.data(), host_bytes);
            }

            code = rig.api.EnqueueCopyImage(rig.queue, a.get(), b.get(), l.origin.data(), l.destination.data(), l.region.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": CopyImage returned " + ClTools::error_name(code));
            } else {
                each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                    std::memcpy(model_b.data() + image_offset(l.shape, l.destination[0] + x, l.destination[1] + y, l.destination[2] + z),
                                model_a.data() + image_offset(l.shape, l.origin[0] + x, l.origin[1] + y, l.origin[2] + z), kElement);
                });
                compare_image(b.get(), model_b, "CopyImage between two images");
            }

            const std::size_t buffer_bytes = 4096;
            ClOwned<cl_mem> staging = Kit::buffer(rig, CL_MEM_READ_WRITE, buffer_bytes, problems, tag + " staging buffer");
            if (staging) {
                std::vector<std::uint8_t> staged(buffer_bytes, 0x3C);
                Kit::expect(problems, tag + ": staging init", Kit::write(rig, staging.get(), staged.data(), buffer_bytes), {CL_SUCCESS});
                const std::size_t dst_offset = 16;
                code = rig.api.EnqueueCopyImageToBuffer(rig.queue, a.get(), staging.get(), l.origin.data(), l.region.data(), dst_offset, 0, nullptr, nullptr);
                if (code != CL_SUCCESS) {
                    problems.add(tag + ": CopyImageToBuffer returned " + ClTools::error_name(code));
                } else {
                    std::size_t at = dst_offset;
                    each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                        std::memcpy(staged.data() + at, model_a.data() + image_offset(l.shape, l.origin[0] + x, l.origin[1] + y, l.origin[2] + z), kElement);
                        at += kElement;
                    });
                    std::vector<std::uint8_t> actual(buffer_bytes);
                    Kit::expect(problems, tag + ": staging read", Kit::read(rig, staging.get(), actual.data(), buffer_bytes), {CL_SUCCESS});
                    Kit::same_bytes(context, problems, tag + " CopyImageToBuffer", staged.data(), actual.data(), buffer_bytes);
                }
                code = rig.api.EnqueueCopyBufferToImage(rig.queue, staging.get(), b.get(), dst_offset, l.destination.data(), l.region.data(), 0, nullptr, nullptr);
                if (code != CL_SUCCESS) {
                    problems.add(tag + ": CopyBufferToImage returned " + ClTools::error_name(code));
                } else {
                    std::size_t at = dst_offset;
                    each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                        std::memcpy(model_b.data() + image_offset(l.shape, l.destination[0] + x, l.destination[1] + y, l.destination[2] + z), staged.data() + at, kElement);
                        at += kElement;
                    });
                    compare_image(b.get(), model_b, "CopyBufferToImage");
                }
            }

            if (rig.version.at_least(1, 2)) {
                const cl_uint color[4] = {10, 20, 30, 40};
                code = rig.api.EnqueueFillImage(rig.queue, b.get(), color, l.origin.data(), l.region.data(), 0, nullptr, nullptr);
                if (code != CL_SUCCESS) {
                    problems.add(tag + ": FillImage (uint color) returned " + ClTools::error_name(code));
                } else {
                    const std::uint8_t texel[4] = {10, 20, 30, 40};
                    each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                        std::memcpy(model_b.data() + image_offset(l.shape, l.origin[0] + x, l.origin[1] + y, l.origin[2] + z), texel, kElement);
                    });
                    compare_image(b.get(), model_b, "FillImage with an unsigned colour");
                }
                const cl_float zero_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                const cl_float color_f[4] = {1.0f, 0.0f, 1.0f, 0.0f};
                Kit::expect(problems, tag + ": FillImage (zero) on UNORM_INT8", rig.api.EnqueueFillImage(rig.queue, other_format.get(), zero_color, zero, whole.data(), 0, nullptr, nullptr), {CL_SUCCESS});
                code = rig.api.EnqueueFillImage(rig.queue, other_format.get(), color_f, l.origin.data(), l.region.data(), 0, nullptr, nullptr);
                if (code != CL_SUCCESS) {
                    problems.add(tag + ": FillImage (float colour) returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint8_t> model_u(bytes, 0);
                    const std::uint8_t texel[4] = {255, 0, 255, 0};
                    each_texel(l.region, [&](std::size_t x, std::size_t y, std::size_t z) {
                        std::memcpy(model_u.data() + image_offset(l.shape, l.origin[0] + x, l.origin[1] + y, l.origin[2] + z), texel, kElement);
                    });
                    compare_image(other_format.get(), model_u, "FillImage with a float colour on UNORM_INT8");
                }
            }

            auto region_of = [&](std::array<std::size_t, 3> r) { return r; };
            std::array<std::size_t, 3> too_far = region_of(whole);
            too_far[0] += 1;
            Kit::expect(problems, tag + ": ReadImage beyond the width", rig.api.EnqueueReadImage(rig.queue, a.get(), CL_TRUE, zero, too_far.data(), 0, 0, sink.data(), 0, nullptr, nullptr), {CL_INVALID_VALUE});
            std::array<std::size_t, 3> empty = region_of(l.region);
            empty[0] = 0;
            Kit::expect(problems, tag + ": ReadImage of an empty region", rig.api.EnqueueReadImage(rig.queue, a.get(), CL_TRUE, zero, empty.data(), 0, 0, sink.data(), 0, nullptr, nullptr), {CL_INVALID_VALUE});
            std::array<std::size_t, 3> shifted = l.origin;
            shifted[0] = whole[0] - l.region[0] + 1;
            Kit::expect(problems, tag + ": WriteImage with an origin pushing the region out", rig.api.EnqueueWriteImage(rig.queue, a.get(), CL_TRUE, shifted.data(), l.region.data(), l.row_pitch, l.slice_pitch, host.data(), 0, nullptr, nullptr), {CL_INVALID_VALUE});
            if (l.shape.type == CL_MEM_OBJECT_IMAGE2D) {
                const std::size_t deep[3] = {0, 0, 1};
                const std::size_t two[3] = {2, 2, 2};
                Kit::expect(problems, tag + ": a 2D image read with origin[2] = 1", rig.api.EnqueueReadImage(rig.queue, a.get(), CL_TRUE, deep, l.region.data(), 0, 0, sink.data(), 0, nullptr, nullptr), {CL_INVALID_VALUE});
                Kit::expect(problems, tag + ": a 2D image read with region[2] = 2", rig.api.EnqueueReadImage(rig.queue, a.get(), CL_TRUE, zero, two, 0, 0, sink.data(), 0, nullptr, nullptr), {CL_INVALID_VALUE});
            }
            Kit::expect(problems, tag + ": CopyImage onto the same region of the same image", rig.api.EnqueueCopyImage(rig.queue, a.get(), a.get(), l.origin.data(), l.origin.data(), l.region.data(), 0, nullptr, nullptr), {CL_MEM_COPY_OVERLAP});
            Kit::expect(problems, tag + ": CopyImage between formats", rig.api.EnqueueCopyImage(rig.queue, a.get(), other_format.get(), l.origin.data(), l.origin.data(), l.region.data(), 0, nullptr, nullptr), {CL_IMAGE_FORMAT_MISMATCH});
        }
        return problems.outcome();
    });
}

Outcome ImageTransferChecks::from_buffer(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.images()) {
            return Context::skip("CL_DEVICE_IMAGE_SUPPORT is CL_FALSE");
        }
        const cl_image_format format{CL_RGBA, CL_UNSIGNED_INT8};
        cl_int code = CL_SUCCESS;
        const std::size_t zero[3] = {0, 0, 0};
        if (rig.version.at_least(1, 2)) {
            const std::size_t width = 64;
            const std::size_t bytes = width * kElement;
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
            if (!buffer) {
                return Outcome::fail("buffer: " + ClTools::error_name(code));
            }
            ImageShape shape{CL_MEM_OBJECT_IMAGE1D_BUFFER, width, 0, 0, 0};
            cl_image_desc desc = ImageKit::descriptor(shape, buffer.get());
            cl_mem raw = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &desc, nullptr, &code);
            ClOwned<cl_mem> image(rig, code == CL_SUCCESS ? raw : nullptr);
            if (!image) {
                problems.add("clCreateImage(image1d_buffer) returned " + ClTools::error_name(code));
            } else {
                cl_mem reported = nullptr;
                rig.api.GetImageInfo(image.get(), CL_IMAGE_BUFFER, sizeof(reported), &reported, nullptr);
                if (reported != buffer.get()) {
                    problems.add("CL_IMAGE_BUFFER does not name the buffer the image was created from");
                }
                const std::vector<std::uint8_t> first = Kit::bytes(1100, bytes);
                Kit::expect(problems, "write of the buffer", Kit::write(rig, buffer.get(), first.data(), bytes), {CL_SUCCESS});
                const std::size_t region[3] = {width, 1, 1};
                std::vector<std::uint8_t> seen(bytes, 0);
                Kit::expect(problems, "ReadImage after a buffer write", rig.api.EnqueueReadImage(rig.queue, image.get(), CL_TRUE, zero, region, 0, 0, seen.data(), 0, nullptr, nullptr), {CL_SUCCESS});
                Kit::same_bytes(context, problems, "image1d_buffer sees buffer writes", first.data(), seen.data(), bytes);
                const std::vector<std::uint8_t> second = Kit::bytes(1101, bytes);
                Kit::expect(problems, "WriteImage", rig.api.EnqueueWriteImage(rig.queue, image.get(), CL_TRUE, zero, region, 0, 0, second.data(), 0, nullptr, nullptr), {CL_SUCCESS});
                std::vector<std::uint8_t> through_buffer(bytes, 0);
                Kit::expect(problems, "read of the buffer", Kit::read(rig, buffer.get(), through_buffer.data(), bytes), {CL_SUCCESS});
                Kit::same_bytes(context, problems, "buffer sees image1d_buffer writes", second.data(), through_buffer.data(), bytes);
            }
            const std::size_t max_buffer = rig.size_info(CL_DEVICE_IMAGE_MAX_BUFFER_SIZE);
            ImageShape too_wide{CL_MEM_OBJECT_IMAGE1D_BUFFER, max_buffer + 1, 0, 0, 0};
            desc = ImageKit::descriptor(too_wide, buffer.get());
            raw = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &desc, nullptr, &code);
            if (raw != nullptr) {
                rig.api.ReleaseMemObject(raw);
                problems.add("an image1d_buffer wider than CL_DEVICE_IMAGE_MAX_BUFFER_SIZE was created");
            } else {
                Kit::expect(problems, "an image1d_buffer wider than CL_DEVICE_IMAGE_MAX_BUFFER_SIZE", code, {CL_INVALID_IMAGE_SIZE, CL_INVALID_IMAGE_DESCRIPTOR});
            }
            ImageShape bigger{CL_MEM_OBJECT_IMAGE1D_BUFFER, width * 2, 0, 0, 0};
            desc = ImageKit::descriptor(bigger, buffer.get());
            raw = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &desc, nullptr, &code);
            if (raw != nullptr) {
                rig.api.ReleaseMemObject(raw);
                problems.add("an image1d_buffer larger than its buffer was created");
            } else {
                Kit::expect(problems, "an image1d_buffer larger than its buffer", code, {CL_INVALID_IMAGE_DESCRIPTOR, CL_INVALID_IMAGE_SIZE, CL_INVALID_BUFFER_SIZE});
            }
        }
        const bool two_d = (rig.version.at_least(2, 0) && !rig.version.at_least(3, 0)) || rig.has_extension("cl_khr_image2d_from_buffer");
        const std::size_t width = 21;
        const std::size_t height = 6;
        const std::size_t align_pixels = std::max<std::size_t>(rig.uint_info(CL_DEVICE_IMAGE_PITCH_ALIGNMENT), 1);
        const std::size_t pitch_pixels = (width + align_pixels - 1) / align_pixels * align_pixels;
        const std::size_t pitch = pitch_pixels * kElement;
        const std::size_t bytes = pitch * height;
        ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
        if (!buffer) {
            problems.add("2D buffer: " + ClTools::error_name(code));
            return problems.outcome();
        }
        cl_image_desc desc{};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = width;
        desc.image_height = height;
        desc.image_row_pitch = pitch;
        desc.mem_object = buffer.get();
        auto make = [&](const cl_image_desc& d, cl_int& result) {
            cl_mem image = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &d, nullptr, &result);
            return ClOwned<cl_mem>(rig, result == CL_SUCCESS ? image : nullptr);
        };
        ClOwned<cl_mem> image = make(desc, code);
        if (!two_d) {
            if (image) {
                problems.add("a 2D image from a buffer was created although OpenCL 2.x and cl_khr_image2d_from_buffer are both absent");
            }
            return problems.outcome();
        }
        if (!image) {
            problems.add("clCreateImage(2D from a buffer, pitch " + std::to_string(pitch) + ") returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        const std::vector<std::uint8_t> first = Kit::bytes(1110, bytes);
        Kit::expect(problems, "write of the 2D buffer", Kit::write(rig, buffer.get(), first.data(), bytes), {CL_SUCCESS});
        const std::size_t region[3] = {width, height, 1};
        std::vector<std::uint8_t> seen(width * height * kElement, 0);
        Kit::expect(problems, "ReadImage of a 2D image from a buffer", rig.api.EnqueueReadImage(rig.queue, image.get(), CL_TRUE, zero, region, 0, 0, seen.data(), 0, nullptr, nullptr), {CL_SUCCESS});
        std::vector<std::uint8_t> expected(seen.size());
        for (std::size_t y = 0; y < height; ++y) {
            std::memcpy(expected.data() + y * width * kElement, first.data() + y * pitch, width * kElement);
        }
        Kit::same_bytes(context, problems, "2D image from a buffer sees buffer writes", expected.data(), seen.data(), seen.size());
        const std::vector<std::uint8_t> second = Kit::bytes(1111, seen.size());
        Kit::expect(problems, "WriteImage of a 2D image from a buffer", rig.api.EnqueueWriteImage(rig.queue, image.get(), CL_TRUE, zero, region, 0, 0, second.data(), 0, nullptr, nullptr), {CL_SUCCESS});
        std::vector<std::uint8_t> through(bytes, 0);
        Kit::expect(problems, "read of the 2D buffer", Kit::read(rig, buffer.get(), through.data(), bytes), {CL_SUCCESS});
        for (std::size_t y = 0; y < height; ++y) {
            Kit::same_bytes(context, problems, "buffer row " + std::to_string(y) + " after an image write", second.data() + y * width * kElement, through.data() + y * pitch, width * kElement);
        }
        cl_image_desc narrow = desc;
        narrow.image_row_pitch = (width - 1) * kElement;
        ClOwned<cl_mem> rejected = make(narrow, code);
        if (rejected) {
            problems.add("a row pitch below width x element size was accepted");
        } else {
            Kit::expect(problems, "a row pitch below width x element size", code, {CL_INVALID_IMAGE_DESCRIPTOR, CL_INVALID_IMAGE_SIZE});
        }
        if (align_pixels > 1) {
            cl_image_desc misaligned = desc;
            misaligned.image_row_pitch = (pitch_pixels + 1) * kElement;
            if ((pitch_pixels + 1) % align_pixels != 0) {
                ClOwned<cl_mem> odd = make(misaligned, code);
                if (odd) {
                    problems.add("a row pitch that is not a multiple of CL_DEVICE_IMAGE_PITCH_ALIGNMENT (" + std::to_string(align_pixels) + " pixels) was accepted");
                } else {
                    Kit::expect(problems, "a row pitch that is not a multiple of CL_DEVICE_IMAGE_PITCH_ALIGNMENT", code, {CL_INVALID_IMAGE_DESCRIPTOR});
                }
            }
        }
        cl_image_desc unaligned = desc;
        unaligned.image_row_pitch = pitch + 1;
        ClOwned<cl_mem> byte_pitch = make(unaligned, code);
        if (byte_pitch) {
            problems.add("a row pitch that is not a multiple of the element size was accepted");
        } else {
            Kit::expect(problems, "a row pitch that is not a multiple of the element size", code, {CL_INVALID_IMAGE_DESCRIPTOR});
        }
        cl_image_desc tall = desc;
        tall.image_height = height * 2;
        ClOwned<cl_mem> too_tall = make(tall, code);
        if (too_tall) {
            problems.add("a 2D image taller than its buffer was accepted");
        } else {
            Kit::expect(problems, "a 2D image taller than its buffer", code, {CL_INVALID_IMAGE_DESCRIPTOR, CL_INVALID_IMAGE_SIZE});
        }
        context.measure("cl.image_pitch_alignment_pixels", align_pixels);
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
