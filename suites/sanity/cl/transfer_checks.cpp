#include "sanity/cl/transfer_checks.hpp"

#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace lsw::suites::sanity::cl {

namespace {

struct Rect {
    std::array<std::size_t, 3> buffer_origin{};
    std::array<std::size_t, 3> host_origin{};
    std::array<std::size_t, 3> region{};
    std::size_t buffer_row_pitch = 0;
    std::size_t buffer_slice_pitch = 0;
    std::size_t host_row_pitch = 0;
    std::size_t host_slice_pitch = 0;
    const char* name = "";
};

std::size_t effective_row(std::size_t pitch, const Rect& rect) {
    return pitch != 0 ? pitch : rect.region[0];
}

std::size_t effective_slice(std::size_t pitch, std::size_t row, const Rect& rect) {
    return pitch != 0 ? pitch : rect.region[1] * row;
}

void model_host_to_buffer(const Rect& rect, const std::vector<std::uint8_t>& host, std::vector<std::uint8_t>& buffer) {
    const std::size_t brp = effective_row(rect.buffer_row_pitch, rect);
    const std::size_t bsp = effective_slice(rect.buffer_slice_pitch, brp, rect);
    const std::size_t hrp = effective_row(rect.host_row_pitch, rect);
    const std::size_t hsp = effective_slice(rect.host_slice_pitch, hrp, rect);
    for (std::size_t z = 0; z < rect.region[2]; ++z) {
        for (std::size_t y = 0; y < rect.region[1]; ++y) {
            for (std::size_t x = 0; x < rect.region[0]; ++x) {
                buffer[(rect.buffer_origin[2] + z) * bsp + (rect.buffer_origin[1] + y) * brp + rect.buffer_origin[0] + x] =
                    host[(rect.host_origin[2] + z) * hsp + (rect.host_origin[1] + y) * hrp + rect.host_origin[0] + x];
            }
        }
    }
}

void model_buffer_to_host(const Rect& rect, const std::vector<std::uint8_t>& buffer, std::vector<std::uint8_t>& host) {
    const std::size_t brp = effective_row(rect.buffer_row_pitch, rect);
    const std::size_t bsp = effective_slice(rect.buffer_slice_pitch, brp, rect);
    const std::size_t hrp = effective_row(rect.host_row_pitch, rect);
    const std::size_t hsp = effective_slice(rect.host_slice_pitch, hrp, rect);
    for (std::size_t z = 0; z < rect.region[2]; ++z) {
        for (std::size_t y = 0; y < rect.region[1]; ++y) {
            for (std::size_t x = 0; x < rect.region[0]; ++x) {
                host[(rect.host_origin[2] + z) * hsp + (rect.host_origin[1] + y) * hrp + rect.host_origin[0] + x] =
                    buffer[(rect.buffer_origin[2] + z) * bsp + (rect.buffer_origin[1] + y) * brp + rect.buffer_origin[0] + x];
            }
        }
    }
}

std::vector<Rect> rect_cases() {
    std::vector<Rect> cases;
    Rect a;
    a.name = "tight 3D";
    a.region = {16, 4, 3};
    cases.push_back(a);
    Rect b;
    b.name = "origins and pitches";
    b.region = {7, 5, 3};
    b.buffer_origin = {3, 1, 1};
    b.host_origin = {2, 2, 0};
    b.buffer_row_pitch = 32;
    b.buffer_slice_pitch = 192;
    b.host_row_pitch = 20;
    b.host_slice_pitch = 160;
    cases.push_back(b);
    Rect c;
    c.name = "2D with a wide host pitch";
    c.region = {13, 9, 1};
    c.buffer_origin = {5, 2, 0};
    c.host_origin = {1, 3, 0};
    c.buffer_row_pitch = 48;
    c.host_row_pitch = 64;
    cases.push_back(c);
    Rect d;
    d.name = "single byte";
    d.region = {1, 1, 1};
    d.buffer_origin = {9, 4, 2};
    d.host_origin = {5, 1, 1};
    d.buffer_row_pitch = 16;
    d.buffer_slice_pitch = 128;
    d.host_row_pitch = 8;
    d.host_slice_pitch = 40;
    cases.push_back(d);
    Rect e;
    e.name = "slice pitch larger than needed";
    e.region = {10, 3, 4};
    e.buffer_origin = {0, 0, 1};
    e.host_origin = {0, 1, 0};
    e.buffer_row_pitch = 10;
    e.buffer_slice_pitch = 60;
    e.host_row_pitch = 12;
    e.host_slice_pitch = 48;
    cases.push_back(e);
    return cases;
}

}  // namespace

Outcome TransferChecks::read_write_blocking_nonblocking(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        std::vector<std::size_t> sizes = {1, 2, 3, 4, 7, 255, 4096, 65537, 1u << 20, 16u << 20};
        const std::uint64_t large_cap = sizeof(void*) == 4 ? (64ull << 20) : (192ull << 20);
        const std::uint64_t big = std::min<std::uint64_t>({large_cap, rig.max_allocation(), rig.global_memory() / 4});
        if (big > sizes.back()) {
            sizes.push_back(static_cast<std::size_t>(big));
        }
        for (const std::size_t size : sizes) {
            const std::string tag = ClTools::bytes_text(size);
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, size, nullptr, code);
            if (!buffer) {
                problems.add(tag + ": clCreateBuffer returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> padded = Kit::bytes(70 + size, size + 1);
            const std::uint8_t* source = padded.data() + 1;
            code = rig.api.EnqueueWriteBuffer(rig.queue, buffer.get(), CL_TRUE, 0, size, source, 0, nullptr, nullptr);
            Kit::expect(problems, tag + ": blocking write from an unaligned host pointer", code, {CL_SUCCESS});
            std::vector<std::uint8_t> back(size + 1, 0xEE);
            code = rig.api.EnqueueReadBuffer(rig.queue, buffer.get(), CL_TRUE, 0, size, back.data() + 1, 0, nullptr, nullptr);
            Kit::expect(problems, tag + ": blocking read into an unaligned host pointer", code, {CL_SUCCESS});
            Kit::same_bytes(context, problems, tag + " blocking round trip", source, back.data() + 1, size);
            if (back[0] != 0xEE) {
                problems.add(tag + ": the read wrote below the destination pointer");
            }

            const std::vector<std::uint8_t> second = Kit::bytes(171 + size, size);
            cl_event write_event = nullptr;
            code = rig.api.EnqueueWriteBuffer(rig.queue, buffer.get(), CL_FALSE, 0, size, second.data(), 0, nullptr, &write_event);
            if (code != CL_SUCCESS || write_event == nullptr) {
                problems.add(tag + ": non-blocking write returned " + ClTools::error_name(code));
                continue;
            }
            ClOwned<cl_event> owned_write(rig, write_event);
            std::vector<std::uint8_t> after(size, 0xEE);
            cl_event read_event = nullptr;
            code = rig.api.EnqueueReadBuffer(rig.queue, buffer.get(), CL_FALSE, 0, size, after.data(), 1, &write_event, &read_event);
            if (code != CL_SUCCESS || read_event == nullptr) {
                problems.add(tag + ": non-blocking read returned " + ClTools::error_name(code));
                rig.api.Finish(rig.queue);
                continue;
            }
            ClOwned<cl_event> owned_read(rig, read_event);
            if (Kit::wait(rig, read_event, problems, tag + " read event")) {
                if (Kit::event_status(rig, read_event) != CL_COMPLETE || Kit::event_status(rig, write_event) != CL_COMPLETE) {
                    problems.add(tag + ": events are not CL_COMPLETE after clWaitForEvents");
                }
                cl_command_type type = 0;
                rig.api.GetEventInfo(read_event, CL_EVENT_COMMAND_TYPE, sizeof(type), &type, nullptr);
                if (type != CL_COMMAND_READ_BUFFER) {
                    problems.add(tag + ": the read event reports command type 0x" + Text::hex(type, 4));
                }
                cl_command_queue queue_of_event = nullptr;
                rig.api.GetEventInfo(read_event, CL_EVENT_COMMAND_QUEUE, sizeof(queue_of_event), &queue_of_event, nullptr);
                if (queue_of_event != rig.queue) {
                    problems.add(tag + ": the read event names a different queue");
                }
                Kit::same_bytes(context, problems, tag + " non-blocking round trip", second.data(), after.data(), size);
            }
        }
        {
            const std::size_t total = 8192;
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, total, nullptr, code);
            if (!buffer) {
                problems.add("offset buffer: " + ClTools::error_name(code));
                return problems.outcome();
            }
            const std::vector<std::uint8_t> base = Kit::bytes(80, total);
            code = Kit::write(rig, buffer.get(), base.data(), total);
            Kit::expect(problems, "offset base write", code, {CL_SUCCESS});
            const std::vector<std::uint8_t> patch = Kit::bytes(81, 1000);
            code = Kit::write(rig, buffer.get(), patch.data(), patch.size(), 3001);
            Kit::expect(problems, "offset write", code, {CL_SUCCESS});
            std::vector<std::uint8_t> model = base;
            std::copy(patch.begin(), patch.end(), model.begin() + 3001);
            std::vector<std::uint8_t> actual(total);
            code = Kit::read(rig, buffer.get(), actual.data(), total);
            Kit::expect(problems, "offset read", code, {CL_SUCCESS});
            Kit::same_bytes(context, problems, "offset write leaves the rest untouched", model.data(), actual.data(), total);
            Kit::expect(problems, "read past the end", rig.api.EnqueueReadBuffer(rig.queue, buffer.get(), CL_TRUE, total - 3, 4, actual.data(), 0, nullptr, nullptr), {CL_INVALID_VALUE});
            Kit::expect(problems, "write with a NULL host pointer", rig.api.EnqueueWriteBuffer(rig.queue, buffer.get(), CL_TRUE, 0, 4, nullptr, 0, nullptr, nullptr), {CL_INVALID_VALUE});
            Kit::expect(problems, "read with a NULL host pointer", rig.api.EnqueueReadBuffer(rig.queue, buffer.get(), CL_TRUE, 0, 4, nullptr, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        }
        context.measure("cl.largest_transfer_bytes", sizes.back());
        return problems.outcome();
    });
}

Outcome TransferChecks::rect_and_fill_copy(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t bytes = 4096;
        if (!rig.version.at_least(1, 1)) {
            return Context::skip("buffer rect commands need OpenCL 1.1; the device reports " + rig.version.text());
        }
        cl_int code = CL_SUCCESS;
        std::uint64_t seed = 200;
        for (const Rect& rect : rect_cases()) {
            const std::string tag = rect.name;
            const std::vector<std::uint8_t> initial = Kit::bytes(++seed, bytes);
            const std::vector<std::uint8_t> host_data = Kit::bytes(++seed, bytes);
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(initial.data()), code);
            if (!buffer) {
                problems.add(tag + ": clCreateBuffer returned " + ClTools::error_name(code));
                continue;
            }
            code = rig.api.EnqueueWriteBufferRect(rig.queue, buffer.get(), CL_TRUE, rect.buffer_origin.data(), rect.host_origin.data(), rect.region.data(), rect.buffer_row_pitch, rect.buffer_slice_pitch, rect.host_row_pitch, rect.host_slice_pitch, host_data.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": clEnqueueWriteBufferRect returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> model = initial;
            model_host_to_buffer(rect, host_data, model);
            std::vector<std::uint8_t> actual(bytes);
            code = Kit::read(rig, buffer.get(), actual.data(), bytes);
            Kit::expect(problems, tag + ": read back", code, {CL_SUCCESS});
            Kit::same_bytes(context, problems, tag + " WriteBufferRect", model.data(), actual.data(), bytes);

            std::vector<std::uint8_t> host(bytes, 0xA5);
            code = rig.api.EnqueueReadBufferRect(rig.queue, buffer.get(), CL_TRUE, rect.buffer_origin.data(), rect.host_origin.data(), rect.region.data(), rect.buffer_row_pitch, rect.buffer_slice_pitch, rect.host_row_pitch, rect.host_slice_pitch, host.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": clEnqueueReadBufferRect returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> host_model(bytes, 0xA5);
            model_buffer_to_host(rect, model, host_model);
            Kit::same_bytes(context, problems, tag + " ReadBufferRect", host_model.data(), host.data(), bytes);

            const std::vector<std::uint8_t> dst_initial = Kit::bytes(++seed, bytes);
            ClOwned<cl_mem> destination = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(dst_initial.data()), code);
            if (!destination) {
                problems.add(tag + ": destination buffer: " + ClTools::error_name(code));
                continue;
            }
            Rect copy = rect;
            code = rig.api.EnqueueCopyBufferRect(rig.queue, buffer.get(), destination.get(), copy.buffer_origin.data(), copy.host_origin.data(), copy.region.data(), copy.buffer_row_pitch, copy.buffer_slice_pitch, copy.host_row_pitch, copy.host_slice_pitch, 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": clEnqueueCopyBufferRect returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> copy_model = dst_initial;
            {
                const std::size_t drp = effective_row(copy.host_row_pitch, copy);
                const std::size_t dsp = effective_slice(copy.host_slice_pitch, drp, copy);
                const std::size_t srp = effective_row(copy.buffer_row_pitch, copy);
                const std::size_t ssp = effective_slice(copy.buffer_slice_pitch, srp, copy);
                for (std::size_t z = 0; z < copy.region[2]; ++z) {
                    for (std::size_t y = 0; y < copy.region[1]; ++y) {
                        for (std::size_t x = 0; x < copy.region[0]; ++x) {
                            copy_model[(copy.host_origin[2] + z) * dsp + (copy.host_origin[1] + y) * drp + copy.host_origin[0] + x] =
                                model[(copy.buffer_origin[2] + z) * ssp + (copy.buffer_origin[1] + y) * srp + copy.buffer_origin[0] + x];
                        }
                    }
                }
            }
            code = Kit::read(rig, destination.get(), actual.data(), bytes);
            Kit::expect(problems, tag + ": read of the copy", code, {CL_SUCCESS});
            Kit::same_bytes(context, problems, tag + " CopyBufferRect", copy_model.data(), actual.data(), bytes);
        }
        {
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
            std::vector<std::uint8_t> host(bytes);
            if (buffer) {
                const std::size_t zero[3] = {0, 0, 0};
                const std::size_t region[3] = {16, 4, 2};
                const std::size_t empty[3] = {0, 4, 2};
                const std::size_t beyond[3] = {bytes, 1, 1};
                auto write_rect = [&](const std::size_t* origin, const std::size_t* extent, std::size_t brp, std::size_t bsp, std::size_t hrp, std::size_t hsp) {
                    return rig.api.EnqueueWriteBufferRect(rig.queue, buffer.get(), CL_TRUE, origin, zero, extent, brp, bsp, hrp, hsp, host.data(), 0, nullptr, nullptr);
                };
                Kit::expect(problems, "WriteBufferRect with a zero-width region", write_rect(zero, empty, 0, 0, 0, 0), {CL_INVALID_VALUE});
                Kit::expect(problems, "WriteBufferRect beyond the buffer", write_rect(beyond, beyond, 0, 0, 0, 0), {CL_INVALID_VALUE});
                Kit::expect(problems, "WriteBufferRect with a row pitch below the region width", write_rect(zero, region, 8, 0, 0, 0), {CL_INVALID_VALUE});
                Kit::expect(problems, "WriteBufferRect with a slice pitch that is below one slice and not a multiple of the row pitch", write_rect(zero, region, 16, 40, 0, 0), {CL_INVALID_VALUE});
                Kit::expect(problems, "WriteBufferRect with a host row pitch below the region width", write_rect(zero, region, 0, 0, 8, 0), {CL_INVALID_VALUE});
                const std::size_t same[3] = {0, 0, 0};
                Kit::expect(problems, "CopyBufferRect onto the same region of the same buffer", rig.api.EnqueueCopyBufferRect(rig.queue, buffer.get(), buffer.get(), same, same, region, 0, 0, 0, 0, 0, nullptr, nullptr), {CL_MEM_COPY_OVERLAP});
            }
        }
        for (const std::size_t pattern_size : {std::size_t(1), std::size_t(2), std::size_t(4), std::size_t(8), std::size_t(16), std::size_t(32), std::size_t(64), std::size_t(128)}) {
            if (!rig.version.at_least(1, 2)) {
                break;
            }
            const std::string tag = "FillBuffer pattern size " + std::to_string(pattern_size);
            const std::vector<std::uint8_t> initial = Kit::bytes(300 + pattern_size, bytes);
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(initial.data()), code);
            if (!buffer) {
                problems.add(tag + ": " + ClTools::error_name(code));
                continue;
            }
            const std::vector<std::uint8_t> pattern = Kit::bytes(400 + pattern_size, pattern_size);
            const std::size_t offset = pattern_size * 3;
            const std::size_t size = pattern_size * 5;
            code = rig.api.EnqueueFillBuffer(rig.queue, buffer.get(), pattern.data(), pattern_size, offset, size, 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> model = initial;
            for (std::size_t i = 0; i < size; ++i) {
                model[offset + i] = pattern[i % pattern_size];
            }
            std::vector<std::uint8_t> actual(bytes);
            code = Kit::read(rig, buffer.get(), actual.data(), bytes);
            Kit::expect(problems, tag + ": read back", code, {CL_SUCCESS});
            Kit::same_bytes(context, problems, tag, model.data(), actual.data(), bytes);
            if (pattern_size > 1) {
                Kit::expect(problems, tag + " with an offset that is not a multiple", rig.api.EnqueueFillBuffer(rig.queue, buffer.get(), pattern.data(), pattern_size, 1, pattern_size, 0, nullptr, nullptr), {CL_INVALID_VALUE});
                Kit::expect(problems, tag + " with a size that is not a multiple", rig.api.EnqueueFillBuffer(rig.queue, buffer.get(), pattern.data(), pattern_size, 0, pattern_size + 1, 0, nullptr, nullptr), {CL_INVALID_VALUE});
            }
            Kit::expect(problems, tag + " beyond the buffer", rig.api.EnqueueFillBuffer(rig.queue, buffer.get(), pattern.data(), pattern_size, bytes - pattern_size, pattern_size * 2, 0, nullptr, nullptr), {CL_INVALID_VALUE});
            Kit::expect(problems, tag + " with a NULL pattern", rig.api.EnqueueFillBuffer(rig.queue, buffer.get(), nullptr, pattern_size, 0, pattern_size, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        }
        if (rig.version.at_least(1, 2)) {
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
            const std::uint8_t pattern[16] = {};
            if (buffer) {
                for (const std::size_t invalid : {std::size_t(0), std::size_t(3), std::size_t(6), std::size_t(256)}) {
                    Kit::expect(problems, "FillBuffer with pattern size " + std::to_string(invalid), rig.api.EnqueueFillBuffer(rig.queue, buffer.get(), pattern, invalid, 0, 12, 0, nullptr, nullptr), {CL_INVALID_VALUE});
                }
            }
        }
        {
            const std::vector<std::uint8_t> initial = Kit::bytes(500, bytes);
            ClOwned<cl_mem> source = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(initial.data()), code);
            const std::vector<std::uint8_t> other = Kit::bytes(501, bytes);
            ClOwned<cl_mem> destination = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(other.data()), code);
            if (source && destination) {
                code = rig.api.EnqueueCopyBuffer(rig.queue, source.get(), destination.get(), 100, 1500, 1000, 0, nullptr, nullptr);
                Kit::expect(problems, "CopyBuffer between buffers", code, {CL_SUCCESS});
                std::vector<std::uint8_t> model = other;
                std::copy(initial.begin() + 100, initial.begin() + 1100, model.begin() + 1500);
                std::vector<std::uint8_t> actual(bytes);
                code = Kit::read(rig, destination.get(), actual.data(), bytes);
                Kit::expect(problems, "read of the copy", code, {CL_SUCCESS});
                Kit::same_bytes(context, problems, "CopyBuffer", model.data(), actual.data(), bytes);
                code = rig.api.EnqueueCopyBuffer(rig.queue, source.get(), source.get(), 0, 2048, 512, 0, nullptr, nullptr);
                Kit::expect(problems, "CopyBuffer within one buffer, disjoint", code, {CL_SUCCESS});
                std::vector<std::uint8_t> self_model = initial;
                std::copy(initial.begin(), initial.begin() + 512, self_model.begin() + 2048);
                code = Kit::read(rig, source.get(), actual.data(), bytes);
                Kit::expect(problems, "read of the self copy", code, {CL_SUCCESS});
                Kit::same_bytes(context, problems, "CopyBuffer within one buffer", self_model.data(), actual.data(), bytes);
                Kit::expect(problems, "CopyBuffer within one buffer, overlapping", rig.api.EnqueueCopyBuffer(rig.queue, source.get(), source.get(), 0, 256, 512, 0, nullptr, nullptr), {CL_MEM_COPY_OVERLAP});
                Kit::expect(problems, "CopyBuffer of a region past the source", rig.api.EnqueueCopyBuffer(rig.queue, source.get(), destination.get(), bytes - 8, 0, 16, 0, nullptr, nullptr), {CL_INVALID_VALUE});
                Kit::expect(problems, "CopyBuffer of a region past the destination", rig.api.EnqueueCopyBuffer(rig.queue, source.get(), destination.get(), 0, bytes - 8, 16, 0, nullptr, nullptr), {CL_INVALID_VALUE});
            }
            const std::size_t align = rig.base_alignment();
            if (rig.version.at_least(1, 1) && align * 4 <= bytes) {
                ClOwned<cl_mem> parent = Kit::buffer(rig, CL_MEM_READ_WRITE, align * 4, nullptr, code);
                if (parent) {
                    cl_buffer_region first_region{0, align * 2};
                    cl_buffer_region second_region{align, align * 2};
                    cl_mem first = rig.api.CreateSubBuffer(parent.get(), 0, CL_BUFFER_CREATE_TYPE_REGION, &first_region, &code);
                    ClOwned<cl_mem> owned_first(rig, code == CL_SUCCESS ? first : nullptr);
                    cl_mem second = rig.api.CreateSubBuffer(parent.get(), 0, CL_BUFFER_CREATE_TYPE_REGION, &second_region, &code);
                    ClOwned<cl_mem> owned_second(rig, code == CL_SUCCESS ? second : nullptr);
                    if (owned_first && owned_second) {
                        Kit::expect(problems, "CopyBuffer between overlapping sub-buffers of one buffer", rig.api.EnqueueCopyBuffer(rig.queue, first, second, 0, 0, align * 2, 0, nullptr, nullptr), {CL_MEM_COPY_OVERLAP});
                    }
                }
            }
        }
        return problems.outcome();
    });
}

Outcome TransferChecks::map_unmap_ranges(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t bytes = 65536;
        const std::vector<std::uint8_t> initial = Kit::bytes(600, bytes);
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(initial.data()), code);
        if (!buffer) {
            return Outcome::fail("clCreateBuffer returned " + ClTools::error_name(code));
        }
        std::vector<std::uint8_t> model = initial;
        auto map = [&](cl_map_flags flags, std::size_t offset, std::size_t size, cl_int& result) {
            return rig.api.EnqueueMapBuffer(rig.queue, buffer.get(), CL_TRUE, flags, offset, size, 0, nullptr, nullptr, &result);
        };
        auto unmap = [&](void* pointer, const std::string& what) {
            Kit::expect(problems, what, rig.api.EnqueueUnmapMemObject(rig.queue, buffer.get(), pointer, 0, nullptr, nullptr), {CL_SUCCESS});
            rig.api.Finish(rig.queue);
        };
        auto verify = [&](const std::string& what) {
            std::vector<std::uint8_t> actual(bytes);
            if (Kit::read(rig, buffer.get(), actual.data(), bytes) == CL_SUCCESS) {
                Kit::same_bytes(context, problems, what, model.data(), actual.data(), bytes);
            } else {
                problems.add(what + ": the verifying read failed");
            }
        };
        {
            void* pointer = map(CL_MAP_READ, 0, bytes, code);
            if (code != CL_SUCCESS || pointer == nullptr) {
                problems.add("map(READ) of the whole buffer returned " + ClTools::error_name(code));
            } else {
                Kit::same_bytes(context, problems, "map(READ) contents", model.data(), pointer, bytes);
                unmap(pointer, "unmap of the READ map");
            }
        }
        {
            void* pointer = map(CL_MAP_WRITE, 4096, 8192, code);
            if (code != CL_SUCCESS || pointer == nullptr) {
                problems.add("map(WRITE) of a sub-range returned " + ClTools::error_name(code));
            } else {
                const std::vector<std::uint8_t> patch = Kit::bytes(601, 8192);
                std::memcpy(pointer, patch.data(), patch.size());
                unmap(pointer, "unmap of the WRITE map");
                std::copy(patch.begin(), patch.end(), model.begin() + 4096);
                verify("write-back of map(WRITE) over a sub-range");
            }
        }
        if (rig.version.at_least(1, 2)) {
            void* pointer = map(CL_MAP_WRITE_INVALIDATE_REGION, 16384, 4096, code);
            if (code != CL_SUCCESS || pointer == nullptr) {
                problems.add("map(WRITE_INVALIDATE_REGION) returned " + ClTools::error_name(code));
            } else {
                const std::vector<std::uint8_t> patch = Kit::bytes(602, 4096);
                std::memcpy(pointer, patch.data(), patch.size());
                unmap(pointer, "unmap of the WRITE_INVALIDATE_REGION map");
                std::copy(patch.begin(), patch.end(), model.begin() + 16384);
                verify("write-back of map(WRITE_INVALIDATE_REGION), bytes outside the region unchanged");
            }
        }
        {
            void* pointer = map(CL_MAP_READ | CL_MAP_WRITE, 0, 1024, code);
            if (code != CL_SUCCESS || pointer == nullptr) {
                problems.add("map(READ|WRITE) returned " + ClTools::error_name(code));
            } else {
                Kit::same_bytes(context, problems, "map(READ|WRITE) initial contents", model.data(), pointer, 1024);
                auto* p = static_cast<std::uint8_t*>(pointer);
                for (std::size_t i = 0; i < 1024; ++i) {
                    p[i] = static_cast<std::uint8_t>(~p[i]);
                    model[i] = static_cast<std::uint8_t>(~model[i]);
                }
                unmap(pointer, "unmap of the READ|WRITE map");
                verify("read-modify-write through map(READ|WRITE)");
            }
        }
        {
            void* pointers[4] = {};
            const std::size_t starts[4] = {20480, 28672, 36864, 45056};
            bool all = true;
            for (int i = 0; i < 4; ++i) {
                pointers[i] = map(CL_MAP_WRITE, starts[i], 1024, code);
                if (code != CL_SUCCESS || pointers[i] == nullptr) {
                    problems.add("simultaneous map " + std::to_string(i) + " returned " + ClTools::error_name(code));
                    all = false;
                }
            }
            if (all) {
                for (int i = 0; i < 4; ++i) {
                    const std::vector<std::uint8_t> patch = Kit::bytes(610 + static_cast<std::uint64_t>(i), 1024);
                    std::memcpy(pointers[i], patch.data(), patch.size());
                    std::copy(patch.begin(), patch.end(), model.begin() + static_cast<std::ptrdiff_t>(starts[i]));
                }
                for (const int i : {2, 0, 3, 1}) {
                    Kit::expect(problems, "unmap of simultaneous map " + std::to_string(i), rig.api.EnqueueUnmapMemObject(rig.queue, buffer.get(), pointers[i], 0, nullptr, nullptr), {CL_SUCCESS});
                }
                rig.api.Finish(rig.queue);
                verify("four simultaneous non-overlapping WRITE maps");
            }
        }
        {
            void* first = map(CL_MAP_READ, 8192, 2048, code);
            void* second = code == CL_SUCCESS ? map(CL_MAP_READ, 8192, 2048, code) : nullptr;
            if (first == nullptr || second == nullptr) {
                problems.add("two overlapping READ maps: " + ClTools::error_name(code));
            } else {
                Kit::same_bytes(context, problems, "first overlapping READ map", model.data() + 8192, first, 2048);
                Kit::same_bytes(context, problems, "second overlapping READ map", model.data() + 8192, second, 2048);
            }
            if (first != nullptr) {
                unmap(first, "unmap of the first overlapping map");
            }
            if (second != nullptr) {
                unmap(second, "unmap of the second overlapping map");
            }
        }
        if (rig.version.at_least(1, 1)) {
            ClProgram program;
            if (Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
                ClKernel step = Kit::kernel(program, "lsw_step", problems);
                if (step && step.set(buffer, std::uint32_t(1), std::uint32_t(0)) == CL_SUCCESS && step.run(bytes / 4) == CL_SUCCESS) {
                    void* pointer = map(CL_MAP_READ, 0, bytes, code);
                    if (code == CL_SUCCESS && pointer != nullptr) {
                        Kit::same_bytes(context, problems, "map after a kernel", model.data(), pointer, bytes);
                        unmap(pointer, "unmap after the kernel map");
                    } else {
                        problems.add("map after a kernel returned " + ClTools::error_name(code));
                    }
                }
            }
        }
        map(CL_MAP_READ, bytes - 4, 8, code);
        Kit::expect(problems, "map past the end", code, {CL_INVALID_VALUE});
        if (rig.version.at_least(1, 2)) {
            map(CL_MAP_READ | CL_MAP_WRITE_INVALIDATE_REGION, 0, 16, code);
            Kit::expect(problems, "map(READ|WRITE_INVALIDATE_REGION)", code, {CL_INVALID_VALUE});
        }
        std::uint8_t stray[8] = {};
        Kit::expect(problems, "unmap of a pointer that was never mapped", rig.api.EnqueueUnmapMemObject(rig.queue, buffer.get(), stray, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        return problems.outcome();
    });
}

Outcome TransferChecks::migrate_mem_objects(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.version.at_least(1, 2) || rig.api.EnqueueMigrateMemObjects == nullptr) {
            return Context::skip("clEnqueueMigrateMemObjects needs OpenCL 1.2; the device reports " + rig.version.text());
        }
        const std::size_t words = 16384;
        const std::size_t bytes = words * 4;
        std::vector<std::uint32_t> a = Kit::words(700, words);
        std::vector<std::uint32_t> b = Kit::words(701, words);
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> first = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, a.data(), code);
        ClOwned<cl_mem> second = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, b.data(), code);
        if (!first || !second) {
            return Outcome::fail("clCreateBuffer returned " + ClTools::error_name(code));
        }
        const cl_mem both[2] = {first.get(), second.get()};
        struct Mode {
            const char* name;
            cl_mem_migration_flags flags;
        };
        for (const Mode& mode : {Mode{"flags 0", 0}, Mode{"CL_MIGRATE_MEM_OBJECT_HOST", CL_MIGRATE_MEM_OBJECT_HOST}}) {
            cl_event event = nullptr;
            code = rig.api.EnqueueMigrateMemObjects(rig.queue, 2, both, mode.flags, 0, nullptr, &event);
            if (code != CL_SUCCESS || event == nullptr) {
                problems.add(std::string("clEnqueueMigrateMemObjects(") + mode.name + ") returned " + ClTools::error_name(code));
                continue;
            }
            ClOwned<cl_event> owned(rig, event);
            if (Kit::wait(rig, event, problems, mode.name)) {
                std::vector<std::uint32_t> got;
                if (Kit::read_words(rig, first.get(), words, got, problems, mode.name)) {
                    Kit::same_words(context, problems, std::string("first buffer after migrate ") + mode.name, a, got);
                }
                if (Kit::read_words(rig, second.get(), words, got, problems, mode.name)) {
                    Kit::same_words(context, problems, std::string("second buffer after migrate ") + mode.name, b, got);
                }
            }
        }
        code = rig.api.EnqueueMigrateMemObjects(rig.queue, 2, both, CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED, 0, nullptr, nullptr);
        Kit::expect(problems, "clEnqueueMigrateMemObjects(CONTENT_UNDEFINED)", code, {CL_SUCCESS});
        rig.api.Finish(rig.queue);
        a = Kit::words(702, words);
        b = Kit::words(703, words);
        Kit::expect(problems, "write after CONTENT_UNDEFINED migration", Kit::write(rig, first.get(), a.data(), bytes), {CL_SUCCESS});
        Kit::expect(problems, "write after CONTENT_UNDEFINED migration", Kit::write(rig, second.get(), b.data(), bytes), {CL_SUCCESS});
        ClProgram program;
        if (Kit::compile(rig, program, ClSources::kVectorAdd, "", problems, "vector add kernel")) {
            ClKernel add = Kit::kernel(program, "lsw_add", problems);
            ClOwned<cl_mem> sum = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
            if (add && sum && add.set(first, second, sum) == CL_SUCCESS) {
                code = add.run(words);
                Kit::expect(problems, "kernel after migration", code, {CL_SUCCESS});
                std::vector<std::uint32_t> expected(words);
                for (std::size_t i = 0; i < words; ++i) {
                    expected[i] = a[i] + b[i];
                }
                std::vector<std::uint32_t> actual;
                if (Kit::read_words(rig, sum.get(), words, actual, problems, "sum")) {
                    Kit::same_words(context, problems, "kernel result after migration", expected, actual);
                }
            }
        }
        Kit::expect(problems, "clEnqueueMigrateMemObjects(0 objects)", rig.api.EnqueueMigrateMemObjects(rig.queue, 0, both, 0, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        Kit::expect(problems, "clEnqueueMigrateMemObjects(NULL list)", rig.api.EnqueueMigrateMemObjects(rig.queue, 1, nullptr, 0, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        Kit::expect(problems, "clEnqueueMigrateMemObjects(invalid flags)", rig.api.EnqueueMigrateMemObjects(rig.queue, 1, both, 4, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
