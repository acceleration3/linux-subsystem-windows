#include "sanity/gl/buffer_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/dxgi_budget.hpp"
#include "support/gl_program.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace lsw::suites::sanity::gl {

namespace {

bool storage_supported(const GlApi& api) {
    return api.supports(4, 4, "GL_ARB_buffer_storage") && api.glBufferStorage != nullptr && api.glMapBufferRange != nullptr &&
           api.glUnmapBuffer != nullptr;
}

std::vector<std::uint8_t> download(const GlApi& api, unsigned buffer, std::size_t size) {
    std::vector<std::uint8_t> data(size, 0xCD);
    api.glBindBuffer(glc::COPY_READ_BUFFER, buffer);
    api.glGetBufferSubData(glc::COPY_READ_BUFFER, 0, static_cast<std::ptrdiff_t>(size), data.data());
    return data;
}

unsigned make_buffer(const GlApi& api, std::size_t size, const void* data, unsigned usage = glc::DYNAMIC_DRAW) {
    unsigned buffer = 0;
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
    api.glBufferData(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(size), data, usage);
    return buffer;
}

unsigned make_storage(const GlApi& api, std::size_t size, const void* data, unsigned flags) {
    unsigned buffer = 0;
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
    api.glBufferStorage(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(size), data, flags);
    return buffer;
}

bool fence_done(const GlApi& api, std::string& detail) {
    unsigned result = 0;
    if (!GlTools::wait_fence(api, 10'000'000'000ull, result)) {
        detail = "the fence wait returned " + GlTools::wait_name(result);
        return false;
    }
    return true;
}

std::string first_difference(const std::vector<std::uint8_t>& expected, const std::uint8_t* actual, std::size_t size) {
    for (std::size_t i = 0; i < size && i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            return "byte " + std::to_string(i) + " is 0x" + Text::hex(actual[i], 2) + ", expected 0x" + Text::hex(expected[i], 2);
        }
    }
    return {};
}

bool equivalent(unsigned type, const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    const std::size_t step = type == glc::UNSIGNED_SHORT ? 2 : type == glc::FLOAT ? 4 : 1;
    for (std::size_t i = 0; i + step <= a.size(); i += step) {
        if (std::memcmp(a.data() + i, b.data() + i, step) == 0) {
            continue;
        }
        if (step == 1) {
            return false;
        }
        long long x = 0;
        long long y = 0;
        for (std::size_t k = 0; k < step; ++k) {
            x |= static_cast<long long>(a[i + k]) << (8 * k);
            y |= static_cast<long long>(b[i + k]) << (8 * k);
        }
        if (std::llabs(x - y) > (step == 2 ? 1 : 4)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Outcome BufferChecks::storage_flag_matrix(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!storage_supported(api)) {
        return Kit::not_advertised("OpenGL 4.4 or GL_ARB_buffer_storage");
    }
    Problems problems;
    constexpr std::size_t kSize = 4096;
    const unsigned bits[] = {glc::MAP_READ_BIT, glc::MAP_WRITE_BIT, glc::MAP_PERSISTENT_BIT, glc::MAP_COHERENT_BIT, glc::DYNAMIC_STORAGE_BIT,
                             glc::CLIENT_STORAGE_BIT};
    int valid_count = 0;
    for (unsigned combo = 0; combo < 64; ++combo) {
        unsigned flags = 0;
        for (unsigned b = 0; b < 6; ++b) {
            flags |= (combo >> b & 1u) != 0 ? bits[b] : 0u;
        }
        const bool persistent = (flags & glc::MAP_PERSISTENT_BIT) != 0;
        const bool valid = !(persistent && (flags & (glc::MAP_READ_BIT | glc::MAP_WRITE_BIT)) == 0) &&
                           !((flags & glc::MAP_COHERENT_BIT) != 0 && !persistent);
        const std::string text = "glBufferStorage with " + GlTools::buffer_flags(flags);
        (void)api.drain_errors();
        unsigned buffer = 0;
        api.glGenBuffers(1, &buffer);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
        api.glBufferStorage(glc::COPY_WRITE_BUFFER, kSize, nullptr, flags);
        if (!valid) {
            GlTools::expect_error(problems, api, text, glc::INVALID_VALUE);
            api.glDeleteBuffers(1, &buffer);
            continue;
        }
        ++valid_count;
        const unsigned failure = api.drain_errors();
        if (failure != 0) {
            problems.add(text + " gave " + GlApi::error_name(failure));
            api.glDeleteBuffers(1, &buffer);
            continue;
        }
        int stored = -1;
        int immutable = -1;
        int size = -1;
        api.glGetBufferParameteriv(glc::COPY_WRITE_BUFFER, glc::BUFFER_STORAGE_FLAGS, &stored);
        api.glGetBufferParameteriv(glc::COPY_WRITE_BUFFER, glc::BUFFER_IMMUTABLE_STORAGE, &immutable);
        api.glGetBufferParameteriv(glc::COPY_WRITE_BUFFER, glc::BUFFER_SIZE, &size);
        if (static_cast<unsigned>(stored) != flags || immutable != 1 || size != static_cast<int>(kSize)) {
            problems.add(text + ": GL_BUFFER_STORAGE_FLAGS " + GlTools::hex(static_cast<unsigned>(stored)) + ", GL_BUFFER_IMMUTABLE_STORAGE " +
                         std::to_string(immutable) + ", GL_BUFFER_SIZE " + std::to_string(size));
        }
        if ((flags & (glc::MAP_READ_BIT | glc::MAP_WRITE_BIT)) != 0) {
            const unsigned access = flags & (glc::MAP_READ_BIT | glc::MAP_WRITE_BIT | glc::MAP_PERSISTENT_BIT | glc::MAP_COHERENT_BIT);
            void* pointer = api.glMapBufferRange(glc::COPY_WRITE_BUFFER, 0, kSize, access);
            const unsigned map_error = api.drain_errors();
            if (pointer == nullptr || map_error != 0) {
                problems.add("glMapBufferRange over the whole buffer with " + GlTools::buffer_flags(access) + " after " + text + " returned " +
                             (pointer == nullptr ? "NULL" : "a pointer") + " and " + GlApi::error_name(map_error));
            } else {
                if ((flags & glc::MAP_WRITE_BIT) != 0) {
                    std::memset(pointer, 0x5A, kSize);
                }
                if ((flags & glc::MAP_READ_BIT) != 0 && (flags & glc::MAP_WRITE_BIT) != 0 && static_cast<std::uint8_t*>(pointer)[kSize - 1] != 0x5A) {
                    problems.add(text + ": a byte written through the mapping reads back different");
                }
                if (api.glUnmapBuffer(glc::COPY_WRITE_BUFFER) == 0) {
                    problems.add("glUnmapBuffer returned GL_FALSE after " + text);
                }
                GlTools::expect_clean(problems, api, "unmapping after " + text);
            }
        }
        api.glDeleteBuffers(1, &buffer);
    }
    {
        unsigned buffer = 0;
        api.glGenBuffers(1, &buffer);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
        api.glBufferStorage(glc::COPY_WRITE_BUFFER, kSize, nullptr, 0x1000);
        GlTools::expect_error(problems, api, "glBufferStorage with an undefined flag bit", glc::INVALID_VALUE);
        api.glBufferStorage(glc::COPY_WRITE_BUFFER, 0, nullptr, 0);
        GlTools::expect_error(problems, api, "glBufferStorage with size 0", glc::INVALID_VALUE);
        api.glBufferStorage(glc::COPY_WRITE_BUFFER, kSize, nullptr, 0);
        GlTools::expect_clean(problems, api, "the first valid glBufferStorage");
        api.glBufferStorage(glc::COPY_WRITE_BUFFER, kSize, nullptr, 0);
        GlTools::expect_error(problems, api, "a second glBufferStorage on the same buffer", glc::INVALID_OPERATION);
        api.glBufferData(glc::COPY_WRITE_BUFFER, kSize, nullptr, glc::STATIC_DRAW);
        GlTools::expect_error(problems, api, "glBufferData on an immutable buffer", glc::INVALID_OPERATION);
        api.glDeleteBuffers(1, &buffer);
    }
    context.measure("valid_combinations", valid_count);
    return problems.outcome();
}

Outcome BufferChecks::persistent_coherent_gpu_write(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!storage_supported(api)) {
        return Kit::not_advertised("OpenGL 4.4 or GL_ARB_buffer_storage");
    }
    constexpr std::size_t kSize = 1024;
    const unsigned storage_flags = glc::MAP_READ_BIT | glc::MAP_WRITE_BIT | glc::MAP_PERSISTENT_BIT | glc::MAP_COHERENT_BIT;
    (void)api.drain_errors();
    const unsigned target = make_storage(api, kSize, nullptr, storage_flags);
    auto* pointer = static_cast<std::uint8_t*>(api.glMapBufferRange(glc::COPY_WRITE_BUFFER, 0, kSize, storage_flags));
    const unsigned map_error = api.drain_errors();
    if (pointer == nullptr || map_error != 0) {
        return Outcome::fail("glMapBufferRange(0, " + std::to_string(kSize) + ", " + GlTools::buffer_flags(storage_flags) + ") on a buffer created with glBufferStorage flags " +
                             GlTools::buffer_flags(storage_flags) + " returned " + (pointer == nullptr ? "NULL" : "a pointer") + " with " + GlApi::error_name(map_error));
    }
    Problems problems;
    int rounds = 0;
    std::string detail;
    for (int round = 0; round < 24; ++round) {
        const std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(300 + round), kSize);
        const unsigned source = make_buffer(api, kSize, data.data());
        std::memset(pointer, 0xEE, kSize);
        api.glBindBuffer(glc::COPY_READ_BUFFER, source);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, target);
        api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, kSize);
        if (!fence_done(api, detail)) {
            problems.add("copy round " + std::to_string(round) + ": " + detail);
            break;
        }
        const std::string difference = first_difference(data, pointer, kSize);
        if (!difference.empty()) {
            problems.add("a coherent persistent mapping does not show a CopyBufferSubData after the fence in round " + std::to_string(round) + ": " + difference);
            break;
        }
        api.glDeleteBuffers(1, &source);
        ++rounds;
    }
    if (api.supports(4, 3, "GL_ARB_compute_shader") && api.glDispatchCompute != nullptr && problems.empty()) {
        GlProgram program;
        const std::string source = "#version 430\nlayout(local_size_x = 64) in;\nlayout(std430, binding = 0) buffer B { int data[]; };\nuniform int seed;\n"
                                   "void main() { data[gl_GlobalInvocationID.x] = int(gl_GlobalInvocationID.x) * 7 + seed; }\n";
        if (!program.build_compute(api, source, error)) {
            problems.add(error);
        } else {
            for (int round = 0; round < 16 && problems.empty(); ++round) {
                std::memset(pointer, 0xEE, kSize);
                program.use();
                api.glUniform1i(program.uniform("seed"), 1000 + round);
                api.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, target);
                api.glDispatchCompute(4, 1, 1);
                if (!fence_done(api, detail)) {
                    problems.add("compute round " + std::to_string(round) + ": " + detail);
                    break;
                }
                for (int i = 0; i < 256; ++i) {
                    int value = 0;
                    std::memcpy(&value, pointer + i * 4, 4);
                    if (value != i * 7 + 1000 + round) {
                        problems.add("a coherent persistent mapping shows " + std::to_string(value) + " at word " + std::to_string(i) + " after a compute shader and a fence in round " +
                                     std::to_string(round) + ", expected " + std::to_string(i * 7 + 1000 + round));
                        break;
                    }
                }
            }
            api.glUseProgram(0);
            program.destroy();
        }
    }
    if (problems.empty()) {
        GlProgram feedback;
        const char* varyings[] = {"v"};
        const std::string vertex = "#version 330 core\nuniform int seed;\nflat out int v;\nvoid main() { v = gl_VertexID * 5 + seed; gl_Position = vec4(0.0); }\n";
        unsigned vao = 0;
        if (!feedback.build_stages(api, vertex, "", varyings, 1, error)) {
            problems.add(error);
        } else {
            api.glGenVertexArrays(1, &vao);
            api.glBindVertexArray(vao);
            for (int round = 0; round < 16 && problems.empty(); ++round) {
                std::memset(pointer, 0xEE, kSize);
                feedback.use();
                api.glUniform1i(feedback.uniform("seed"), 77 + round);
                api.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, target);
                api.glEnable(glc::RASTERIZER_DISCARD);
                api.glBeginTransformFeedback(glc::POINTS);
                api.glDrawArrays(glc::POINTS, 0, 256);
                api.glEndTransformFeedback();
                api.glDisable(glc::RASTERIZER_DISCARD);
                if (!fence_done(api, detail)) {
                    problems.add("transform feedback round " + std::to_string(round) + ": " + detail);
                    break;
                }
                for (int i = 0; i < 256; ++i) {
                    int value = 0;
                    std::memcpy(&value, pointer + i * 4, 4);
                    if (value != i * 5 + 77 + round) {
                        problems.add("a coherent persistent mapping shows " + std::to_string(value) + " at word " + std::to_string(i) + " after transform feedback and a fence in round " +
                                     std::to_string(round) + ", expected " + std::to_string(i * 5 + 77 + round));
                        break;
                    }
                }
            }
            api.glBindVertexArray(0);
            api.glDeleteVertexArrays(1, &vao);
            api.glUseProgram(0);
            feedback.destroy();
        }
    }
    GlTools::expect_clean(problems, api, "the persistent coherent rounds");
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, target);
    api.glUnmapBuffer(glc::COPY_WRITE_BUFFER);
    api.glFinish();
    api.glDeleteBuffers(1, &target);
    context.measure("copy_rounds", rounds);
    return problems.outcome();
}

Outcome BufferChecks::persistent_noncoherent_flush(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!storage_supported(api) || api.glFlushMappedBufferRange == nullptr || api.glMemoryBarrier == nullptr) {
        return Kit::not_advertised("OpenGL 4.4 or GL_ARB_buffer_storage with glMemoryBarrier");
    }
    constexpr std::size_t kSize = 1024;
    const unsigned storage_flags = glc::MAP_READ_BIT | glc::MAP_WRITE_BIT | glc::MAP_PERSISTENT_BIT;
    const unsigned map_flags = storage_flags | glc::MAP_FLUSH_EXPLICIT_BIT;
    (void)api.drain_errors();
    const unsigned mapped = make_storage(api, kSize, nullptr, storage_flags);
    auto* pointer = static_cast<std::uint8_t*>(api.glMapBufferRange(glc::COPY_WRITE_BUFFER, 0, kSize, map_flags));
    const unsigned map_error = api.drain_errors();
    if (pointer == nullptr || map_error != 0) {
        return Outcome::fail("glMapBufferRange(" + GlTools::buffer_flags(map_flags) + ") on storage " + GlTools::buffer_flags(storage_flags) + " returned " +
                             (pointer == nullptr ? "NULL" : "a pointer") + " with " + GlApi::error_name(map_error));
    }
    Problems problems;
    std::string detail;
    const unsigned sink = make_buffer(api, kSize, nullptr);
    for (int round = 0; round < 16; ++round) {
        const std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(700 + round), kSize);
        std::memcpy(pointer, data.data(), kSize);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, mapped);
        api.glFlushMappedBufferRange(glc::COPY_WRITE_BUFFER, 0, kSize);
        api.glBindBuffer(glc::COPY_READ_BUFFER, mapped);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, sink);
        api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, kSize);
        const std::vector<std::uint8_t> got = download(api, sink, kSize);
        const std::string difference = first_difference(data, got.data(), kSize);
        if (!difference.empty()) {
            problems.add("host writes through a non-coherent persistent mapping are not seen by the GPU after glFlushMappedBufferRange in round " + std::to_string(round) + ": " + difference);
            break;
        }
    }
    int gpu_rounds = 0;
    for (int round = 0; round < 16 && problems.empty(); ++round) {
        const std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(800 + round), kSize);
        const unsigned source = make_buffer(api, kSize, data.data());
        std::memset(pointer, 0xEE, kSize);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, mapped);
        api.glFlushMappedBufferRange(glc::COPY_WRITE_BUFFER, 0, kSize);
        api.glBindBuffer(glc::COPY_READ_BUFFER, source);
        api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, kSize);
        api.glMemoryBarrier(glc::CLIENT_MAPPED_BUFFER_BARRIER_BIT);
        if (!fence_done(api, detail)) {
            problems.add("GPU write round " + std::to_string(round) + ": " + detail);
            break;
        }
        const std::string difference = first_difference(data, pointer, kSize);
        if (!difference.empty()) {
            problems.add("a non-coherent persistent mapping does not show a GPU write after MemoryBarrier(CLIENT_MAPPED_BUFFER_BARRIER_BIT) and a fence in round " +
                         std::to_string(round) + ": " + difference);
            break;
        }
        api.glDeleteBuffers(1, &source);
        ++gpu_rounds;
    }
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, mapped);
    api.glUnmapBuffer(glc::COPY_WRITE_BUFFER);
    api.glDeleteBuffers(1, &mapped);
    const unsigned plain = make_storage(api, kSize, nullptr, storage_flags);
    auto* plain_pointer = static_cast<std::uint8_t*>(api.glMapBufferRange(glc::COPY_WRITE_BUFFER, 0, kSize, storage_flags));
    if (plain_pointer != nullptr) {
        int barrier_ok = 0;
        for (int round = 0; round < 8; ++round) {
            const std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(1200 + round), kSize);
            std::memcpy(plain_pointer, data.data(), kSize);
            api.glMemoryBarrier(glc::CLIENT_MAPPED_BUFFER_BARRIER_BIT);
            api.glBindBuffer(glc::COPY_READ_BUFFER, plain);
            api.glBindBuffer(glc::COPY_WRITE_BUFFER, sink);
            api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, kSize);
            const std::vector<std::uint8_t> got = download(api, sink, kSize);
            barrier_ok += first_difference(data, got.data(), kSize).empty() ? 1 : 0;
        }
        context.measure("host_write_barrier_only_ok_of_8", barrier_ok);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, plain);
        api.glUnmapBuffer(glc::COPY_WRITE_BUFFER);
    }
    api.glFinish();
    api.glDeleteBuffers(1, &plain);
    api.glDeleteBuffers(1, &sink);
    GlTools::expect_clean(problems, api, "the non-coherent rounds");
    context.measure("gpu_write_rounds", gpu_rounds);
    return problems.outcome();
}

Outcome BufferChecks::dynamic_storage_subdata(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!storage_supported(api)) {
        return Kit::not_advertised("OpenGL 4.4 or GL_ARB_buffer_storage");
    }
    constexpr std::size_t kSize = 1024;
    std::vector<std::uint8_t> shadow = Kit::bytes(40, kSize);
    (void)api.drain_errors();
    const unsigned dynamic = make_storage(api, kSize, shadow.data(), glc::DYNAMIC_STORAGE_BIT);
    Problems problems;
    GlTools::expect_clean(problems, api, "creating the DYNAMIC_STORAGE buffer");
    struct Update {
        std::size_t offset;
        std::size_t size;
    };
    const Update updates[] = {{0, 1024}, {1, 3}, {3, 5}, {7, 1}, {13, 255}, {1000, 24}, {512, 0}, {1023, 1}, {100, 100}, {2, 1021}, {5, 17}};
    unsigned seed = 50;
    for (const Update& update : updates) {
        const std::vector<std::uint8_t> data = Kit::bytes(seed++, std::max<std::size_t>(update.size, 1));
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, dynamic);
        api.glBufferSubData(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(update.offset), static_cast<std::ptrdiff_t>(update.size), data.data());
        const std::string text = "glBufferSubData(offset " + std::to_string(update.offset) + ", size " + std::to_string(update.size) + ")";
        GlTools::expect_clean(problems, api, text);
        std::memcpy(shadow.data() + update.offset, data.data(), update.size);
        const std::vector<std::uint8_t> got = download(api, dynamic, kSize);
        const std::string difference = first_difference(shadow, got.data(), kSize);
        if (!difference.empty()) {
            problems.add(text + " leaves the buffer different from the CPU shadow: " + difference);
            break;
        }
    }
    const std::vector<std::uint8_t> filler = Kit::bytes(99, 64);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, dynamic);
    api.glBufferSubData(glc::COPY_WRITE_BUFFER, 1020, 8, filler.data());
    GlTools::expect_error(problems, api, "glBufferSubData past the end", glc::INVALID_VALUE);
    api.glBufferSubData(glc::COPY_WRITE_BUFFER, -1, 4, filler.data());
    GlTools::expect_error(problems, api, "glBufferSubData with a negative offset", glc::INVALID_VALUE);
    if (first_difference(shadow, download(api, dynamic, kSize).data(), kSize).size() != 0) {
        problems.add("a rejected glBufferSubData changed the buffer");
    }
    const std::vector<std::uint8_t> initial = Kit::bytes(41, kSize);
    const unsigned flag_sets[] = {0, glc::MAP_WRITE_BIT, glc::MAP_READ_BIT | glc::MAP_WRITE_BIT};
    for (const unsigned flags : flag_sets) {
        const unsigned fixed = make_storage(api, kSize, initial.data(), flags);
        api.glBufferSubData(glc::COPY_WRITE_BUFFER, 0, 16, filler.data());
        GlTools::expect_error(problems, api, "glBufferSubData on storage " + GlTools::buffer_flags(flags) + " without DYNAMIC_STORAGE", glc::INVALID_OPERATION);
        const std::vector<std::uint8_t> got = download(api, fixed, kSize);
        if (!first_difference(initial, got.data(), kSize).empty()) {
            problems.add("a rejected glBufferSubData changed the content of storage " + GlTools::buffer_flags(flags));
        }
        api.glDeleteBuffers(1, &fixed);
    }
    api.glDeleteBuffers(1, &dynamic);
    return problems.outcome();
}

Outcome BufferChecks::map_range_flags(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 0, "GL_ARB_map_buffer_range") || api.glMapBufferRange == nullptr) {
        return Kit::not_advertised("OpenGL 3.0 or GL_ARB_map_buffer_range");
    }
    constexpr std::size_t kSize = 4096;
    Problems problems;
    std::string detail;
    auto write_range = [&](unsigned buffer, std::size_t offset, std::size_t length, unsigned flags, const std::vector<std::uint8_t>& data, bool explicit_flush,
                           std::size_t flush_offset, std::size_t flush_length) {
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
        void* pointer = api.glMapBufferRange(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(length), flags);
        const unsigned failure = api.drain_errors();
        if (pointer == nullptr || failure != 0) {
            problems.add("glMapBufferRange(" + std::to_string(offset) + ", " + std::to_string(length) + ", " + GlTools::buffer_flags(flags) + ") returned " +
                         (pointer == nullptr ? "NULL" : "a pointer") + " with " + GlApi::error_name(failure));
            return false;
        }
        std::memcpy(pointer, data.data(), length);
        if (explicit_flush) {
            api.glFlushMappedBufferRange(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(flush_offset), static_cast<std::ptrdiff_t>(flush_length));
        }
        if (api.glUnmapBuffer(glc::COPY_WRITE_BUFFER) == 0) {
            problems.add("glUnmapBuffer returned GL_FALSE after " + GlTools::buffer_flags(flags));
            return false;
        }
        return true;
    };
    {
        std::vector<std::uint8_t> shadow = Kit::bytes(60, kSize);
        const unsigned buffer = make_buffer(api, kSize, shadow.data());
        const std::vector<std::uint8_t> data = Kit::bytes(61, 1024);
        if (write_range(buffer, 1024, 1024, glc::MAP_WRITE_BIT | glc::MAP_INVALIDATE_RANGE_BIT, data, false, 0, 0)) {
            std::memcpy(shadow.data() + 1024, data.data(), 1024);
            const std::string difference = first_difference(shadow, download(api, buffer, kSize).data(), kSize);
            if (!difference.empty()) {
                problems.add("MAP_INVALIDATE_RANGE over [1024,2048) changed bytes outside the range or lost the written data: " + difference);
            }
        }
        const std::vector<std::uint8_t> whole = Kit::bytes(62, kSize);
        if (write_range(buffer, 0, kSize, glc::MAP_WRITE_BIT | glc::MAP_INVALIDATE_BUFFER_BIT, whole, false, 0, 0)) {
            const std::string difference = first_difference(whole, download(api, buffer, kSize).data(), kSize);
            if (!difference.empty()) {
                problems.add("MAP_INVALIDATE_BUFFER over the whole buffer lost the written data: " + difference);
            }
        }
        const std::vector<std::uint8_t> part = Kit::bytes(63, 512);
        if (write_range(buffer, 512, 512, glc::MAP_WRITE_BIT | glc::MAP_INVALIDATE_BUFFER_BIT, part, false, 0, 0)) {
            const std::vector<std::uint8_t> got = download(api, buffer, kSize);
            if (std::memcmp(got.data() + 512, part.data(), 512) != 0) {
                problems.add("MAP_INVALIDATE_BUFFER over a sub-range lost the written data");
            }
        }
        api.glDeleteBuffers(1, &buffer);
    }
    {
        std::vector<std::uint8_t> shadow = Kit::bytes(64, kSize);
        const std::vector<std::uint8_t> source_data = Kit::bytes(65, 1024);
        const unsigned buffer = make_buffer(api, kSize, shadow.data());
        const unsigned source = make_buffer(api, 1024, source_data.data());
        api.glBindBuffer(glc::COPY_READ_BUFFER, source);
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
        api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, 1024);
        std::memcpy(shadow.data(), source_data.data(), 1024);
        if (!fence_done(api, detail)) {
            problems.add("UNSYNCHRONIZED setup: " + detail);
        } else {
            const std::vector<std::uint8_t> data = Kit::bytes(66, 1024);
            if (write_range(buffer, 2048, 1024, glc::MAP_WRITE_BIT | glc::MAP_UNSYNCHRONIZED_BIT, data, false, 0, 0)) {
                std::memcpy(shadow.data() + 2048, data.data(), 1024);
                const std::string difference = first_difference(shadow, download(api, buffer, kSize).data(), kSize);
                if (!difference.empty()) {
                    problems.add("MAP_UNSYNCHRONIZED after a fenced GPU copy gives wrong data: " + difference);
                }
            }
        }
        api.glDeleteBuffers(1, &source);
        api.glDeleteBuffers(1, &buffer);
    }
    {
        const std::vector<std::uint8_t> old = Kit::bytes(67, kSize);
        const unsigned buffer = make_buffer(api, kSize, old.data());
        const std::vector<std::uint8_t> data = Kit::bytes(68, kSize);
        if (write_range(buffer, 0, kSize, glc::MAP_WRITE_BIT | glc::MAP_FLUSH_EXPLICIT_BIT, data, true, 0, 512)) {
            const std::vector<std::uint8_t> got = download(api, buffer, kSize);
            if (std::memcmp(got.data(), data.data(), 512) != 0) {
                problems.add("MAP_FLUSH_EXPLICIT: the flushed range [0,512) does not hold the written data after unmapping");
            }
            for (std::size_t i = 1024; i < 1536; ++i) {
                if (got[i] != old[i] && got[i] != data[i]) {
                    problems.add("MAP_FLUSH_EXPLICIT: unflushed byte " + std::to_string(i) + " holds 0x" + Text::hex(got[i], 2) + ", neither the old 0x" + Text::hex(old[i], 2) +
                                 " nor the written 0x" + Text::hex(data[i], 2));
                    break;
                }
            }
        }
        api.glDeleteBuffers(1, &buffer);
    }
    GlTools::expect_clean(problems, api, "the map range cases");
    return problems.outcome();
}

Outcome BufferChecks::map_errors(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 0, "GL_ARB_map_buffer_range") || api.glMapBufferRange == nullptr) {
        return Kit::not_advertised("OpenGL 3.0 or GL_ARB_map_buffer_range");
    }
    constexpr std::ptrdiff_t kSize = 256;
    const std::vector<std::uint8_t> data = Kit::bytes(70, static_cast<std::size_t>(kSize));
    const unsigned buffer = make_buffer(api, static_cast<std::size_t>(kSize), data.data());
    api.glBindBuffer(glc::ARRAY_BUFFER, buffer);
    Problems problems;
    (void)api.drain_errors();
    struct Case {
        const char* name;
        std::ptrdiff_t offset;
        std::ptrdiff_t length;
        unsigned flags;
        unsigned error;
    };
    const Case cases[] = {
        {"length past the end", 0, kSize + 1, glc::MAP_WRITE_BIT, glc::INVALID_VALUE},
        {"offset plus length past the end", kSize - 1, 2, glc::MAP_READ_BIT, glc::INVALID_VALUE},
        {"offset equal to the size", kSize, 1, glc::MAP_READ_BIT, glc::INVALID_VALUE},
        {"negative offset", -1, 4, glc::MAP_READ_BIT, glc::INVALID_VALUE},
        {"no READ or WRITE bit", 0, 16, 0, glc::INVALID_OPERATION},
        {"INVALIDATE_RANGE with READ", 0, 16, glc::MAP_READ_BIT | glc::MAP_INVALIDATE_RANGE_BIT, glc::INVALID_OPERATION},
        {"INVALIDATE_BUFFER with READ", 0, 16, glc::MAP_READ_BIT | glc::MAP_INVALIDATE_BUFFER_BIT, glc::INVALID_OPERATION},
        {"FLUSH_EXPLICIT without WRITE", 0, 16, glc::MAP_READ_BIT | glc::MAP_FLUSH_EXPLICIT_BIT, glc::INVALID_OPERATION},
        {"UNSYNCHRONIZED with READ", 0, 16, glc::MAP_READ_BIT | glc::MAP_UNSYNCHRONIZED_BIT, glc::INVALID_OPERATION},
    };
    for (const Case& item : cases) {
        void* pointer = api.glMapBufferRange(glc::ARRAY_BUFFER, item.offset, item.length, item.flags);
        const std::string text = std::string("glMapBufferRange with ") + item.name;
        if (item.error == glc::NO_ERROR_) {
            GlTools::expect_clean(problems, api, text);
            if (pointer == nullptr) {
                problems.add(text + " returned NULL");
            } else if (api.glUnmapBuffer(glc::ARRAY_BUFFER) == 0) {
                problems.add("glUnmapBuffer returned GL_FALSE after " + text);
            }
        } else {
            GlTools::expect_error(problems, api, text, item.error);
            if (pointer != nullptr) {
                problems.add(text + " returned a pointer");
                api.glUnmapBuffer(glc::ARRAY_BUFFER);
            }
        }
    }
    {
        void* empty = api.glMapBufferRange(glc::ARRAY_BUFFER, 0, 0, glc::MAP_READ_BIT);
        const unsigned zero_error = api.drain_errors();
        context.measure("zero_length_map_error", GlApi::error_name(zero_error));
        if ((zero_error != glc::INVALID_VALUE && zero_error != glc::INVALID_OPERATION) || empty != nullptr) {
            problems.add("glMapBufferRange with zero length gave " + GlApi::error_name(zero_error) + ", expected GL_INVALID_OPERATION (4.5) or GL_INVALID_VALUE");
        }
    }
    void* first = api.glMapBufferRange(glc::ARRAY_BUFFER, 0, 64, glc::MAP_READ_BIT);
    GlTools::expect_clean(problems, api, "the first mapping");
    if (first == nullptr) {
        problems.add("a valid read mapping returned NULL");
    } else {
        void* second = api.glMapBufferRange(glc::ARRAY_BUFFER, 64, 64, glc::MAP_READ_BIT);
        GlTools::expect_error(problems, api, "mapping an already mapped buffer", glc::INVALID_OPERATION);
        if (second != nullptr) {
            problems.add("mapping an already mapped buffer returned a pointer");
        }
        api.glBufferSubData(glc::ARRAY_BUFFER, 0, 4, data.data());
        GlTools::expect_error(problems, api, "glBufferSubData on a mapped buffer", glc::INVALID_OPERATION);
        if (std::memcmp(first, data.data(), 64) != 0) {
            problems.add("the mapped bytes differ from the uploaded data");
        }
        if (api.glUnmapBuffer(glc::ARRAY_BUFFER) != 1) {
            problems.add("glUnmapBuffer returned GL_FALSE on an intact mapping");
        }
        GlTools::expect_clean(problems, api, "unmapping");
    }
    api.glUnmapBuffer(glc::ARRAY_BUFFER);
    GlTools::expect_error(problems, api, "glUnmapBuffer on an unmapped buffer", glc::INVALID_OPERATION);
    api.glBindBuffer(glc::ARRAY_BUFFER, 0);
    void* none = api.glMapBufferRange(glc::ARRAY_BUFFER, 0, 16, glc::MAP_READ_BIT);
    GlTools::expect_error(problems, api, "glMapBufferRange with no buffer bound", glc::INVALID_OPERATION);
    if (none != nullptr) {
        problems.add("mapping with no buffer bound returned a pointer");
    }
    api.glDeleteBuffers(1, &buffer);
    return problems.outcome();
}

Outcome BufferChecks::copy_and_clear(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 1, "GL_ARB_copy_buffer") || api.glCopyBufferSubData == nullptr) {
        return Kit::not_advertised("OpenGL 3.1 or GL_ARB_copy_buffer");
    }
    constexpr std::size_t kSize = 65536;
    std::vector<std::uint8_t> source_data = Kit::bytes(80, kSize);
    std::vector<std::uint8_t> shadow = Kit::bytes(81, kSize);
    const unsigned source = make_buffer(api, kSize, source_data.data());
    const unsigned destination = make_buffer(api, kSize, shadow.data());
    Problems problems;
    const std::size_t sizes[] = {1, 2, 3, 7, 16, 255, 256, 4093, 4096, 65000};
    const std::size_t offsets[] = {0, 1, 3, 13, 100};
    int copies = 0;
    for (const std::size_t size : sizes) {
        for (const std::size_t read_offset : offsets) {
            const std::size_t write_offset = offsets[(copies + 2) % 5];
            if (read_offset + size > kSize || write_offset + size > kSize) {
                continue;
            }
            api.glBindBuffer(glc::COPY_READ_BUFFER, source);
            api.glBindBuffer(glc::COPY_WRITE_BUFFER, destination);
            api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(read_offset), static_cast<std::ptrdiff_t>(write_offset),
                                    static_cast<std::ptrdiff_t>(size));
            std::memcpy(shadow.data() + write_offset, source_data.data() + read_offset, size);
            ++copies;
        }
        const std::string difference = first_difference(shadow, download(api, destination, kSize).data(), kSize);
        if (!difference.empty()) {
            problems.add("after CopyBufferSubData copies of " + std::to_string(size) + " bytes the destination differs from the CPU reference: " + difference);
            break;
        }
    }
    GlTools::expect_clean(problems, api, "the copies");
    api.glBindBuffer(glc::COPY_READ_BUFFER, destination);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, destination);
    api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 8, 16);
    GlTools::expect_error(problems, api, "CopyBufferSubData with overlapping ranges of one buffer", glc::INVALID_VALUE);
    std::memmove(shadow.data() + 16, shadow.data(), 16);
    api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 16, 16);
    GlTools::expect_clean(problems, api, "CopyBufferSubData with adjacent ranges of one buffer");
    if (!first_difference(shadow, download(api, destination, kSize).data(), kSize).empty()) {
        problems.add("adjacent same-buffer copy produced wrong bytes");
    }
    api.glBindBuffer(glc::COPY_READ_BUFFER, source);
    api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(kSize - 4), 0, 8);
    GlTools::expect_error(problems, api, "CopyBufferSubData reading past the end", glc::INVALID_VALUE);
    api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, static_cast<std::ptrdiff_t>(kSize - 4), 8);
    GlTools::expect_error(problems, api, "CopyBufferSubData writing past the end", glc::INVALID_VALUE);
    if (api.supports(4, 3, "GL_ARB_clear_buffer_object") && api.glClearBufferSubData != nullptr) {
        api.glBindBuffer(glc::COPY_WRITE_BUFFER, destination);
        const std::uint8_t byte_value = 0xAB;
        api.glClearBufferSubData(glc::COPY_WRITE_BUFFER, glc::R8, 3, 100, glc::RED, glc::UNSIGNED_BYTE, &byte_value);
        GlTools::expect_clean(problems, api, "ClearBufferSubData R8");
        std::memset(shadow.data() + 3, 0xAB, 100);
        const std::uint32_t word_value = 0xDEADBEEFu;
        api.glClearBufferSubData(glc::COPY_WRITE_BUFFER, glc::R32UI, 8, 64, glc::RED_INTEGER, glc::UNSIGNED_INT, &word_value);
        GlTools::expect_clean(problems, api, "ClearBufferSubData R32UI");
        for (std::size_t i = 0; i < 64; i += 4) {
            std::memcpy(shadow.data() + 8 + i, &word_value, 4);
        }
        api.glClearBufferSubData(glc::COPY_WRITE_BUFFER, glc::R32UI, 2, 64, glc::RED_INTEGER, glc::UNSIGNED_INT, &word_value);
        GlTools::expect_error(problems, api, "ClearBufferSubData R32UI with an offset that is not a multiple of 4", glc::INVALID_VALUE);
        const std::string difference = first_difference(shadow, download(api, destination, kSize).data(), kSize);
        if (!difference.empty()) {
            problems.add("ClearBufferSubData differs from the CPU reference: " + difference);
        }
    } else {
        context.note("ClearBufferSubData is not advertised; only the copy rows ran");
    }
    api.glDeleteBuffers(1, &source);
    api.glDeleteBuffers(1, &destination);
    if (!problems.empty()) {
        return problems.outcome();
    }
    VideoMemory memory;
    std::string budget_error;
    std::uint64_t free_bytes = 64ull << 20;
    if (DxgiBudget::query(rig.adapter.luid, true, memory, budget_error) && memory.budget > memory.usage) {
        free_bytes = memory.budget - memory.usage;
    }
    const std::size_t large = static_cast<std::size_t>(std::clamp<std::uint64_t>(free_bytes / 6, 8ull << 20, 128ull << 20)) & ~static_cast<std::size_t>(0xFFFFF);
    context.measure("large_bytes", static_cast<long long>(large));
    const auto begin = std::chrono::steady_clock::now();
    const unsigned a = make_buffer(api, large, nullptr);
    const unsigned b = make_buffer(api, large, nullptr);
    const unsigned alloc_error = api.drain_errors();
    if (alloc_error != 0) {
        api.glDeleteBuffers(1, &a);
        api.glDeleteBuffers(1, &b);
        return Outcome::fail("allocating two " + std::to_string(large >> 20) + " MiB buffers within the " + std::to_string(free_bytes >> 20) + " MiB budget gave " + GlApi::error_name(alloc_error));
    }
    constexpr std::size_t kChunk = 4u << 20;
    std::vector<std::uint8_t> chunk(kChunk);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, a);
    for (std::size_t offset = 0; offset < large; offset += kChunk) {
        Pattern::fill(90, offset, chunk.data(), kChunk);
        api.glBufferSubData(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(kChunk), chunk.data());
    }
    api.glBindBuffer(glc::COPY_READ_BUFFER, a);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, b);
    api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, static_cast<std::ptrdiff_t>(large));
    const std::uint8_t fill = 0x5A;
    const std::size_t clear_begin = large / 4;
    const std::size_t clear_size = large / 4;
    if (api.glClearBufferSubData != nullptr && api.supports(4, 3, "GL_ARB_clear_buffer_object")) {
        api.glClearBufferSubData(glc::COPY_WRITE_BUFFER, glc::R8, static_cast<std::ptrdiff_t>(clear_begin), static_cast<std::ptrdiff_t>(clear_size), glc::RED, glc::UNSIGNED_BYTE, &fill);
    }
    const bool cleared = api.glClearBufferSubData != nullptr && api.supports(4, 3, "GL_ARB_clear_buffer_object");
    std::vector<std::uint8_t> expected(kChunk);
    std::vector<std::uint8_t> got(kChunk);
    api.glBindBuffer(glc::COPY_READ_BUFFER, b);
    for (std::size_t offset = 0; offset < large && problems.empty(); offset += kChunk) {
        Pattern::fill(90, offset, expected.data(), kChunk);
        if (cleared) {
            for (std::size_t i = 0; i < kChunk; ++i) {
                if (offset + i >= clear_begin && offset + i < clear_begin + clear_size) {
                    expected[i] = fill;
                }
            }
        }
        api.glGetBufferSubData(glc::COPY_READ_BUFFER, static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(kChunk), got.data());
        if (std::memcmp(expected.data(), got.data(), kChunk) != 0) {
            std::size_t at = 0;
            while (expected[at] == got[at]) {
                ++at;
            }
            problems.add("the " + std::to_string(large >> 20) + " MiB copy differs at byte " + std::to_string(offset + at) + ": 0x" + Text::hex(got[at], 2) + ", expected 0x" +
                         Text::hex(expected[at], 2));
        }
    }
    api.glFinish();
    GlTools::expect_clean(problems, api, "the large copy");
    api.glDeleteBuffers(1, &a);
    api.glDeleteBuffers(1, &b);
    context.measure("large_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count());
    return problems.outcome();
}

Outcome BufferChecks::pbo_pack_unpack(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(2, 1, "GL_ARB_pixel_buffer_object")) {
        return Kit::not_advertised("OpenGL 2.1 or GL_ARB_pixel_buffer_object");
    }
    constexpr int kSide = 16;
    const std::vector<std::uint8_t> source_pixels = Kit::bytes(110, kSide * kSide * 4);
    unsigned texture = 0;
    unsigned fbo = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D, texture);
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), kSide, kSide, 0, glc::RGBA, glc::UNSIGNED_BYTE, source_pixels.data());
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
    api.glGenFramebuffers(1, &fbo);
    api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture, 0);
    if (api.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
        return Outcome::fail("the source framebuffer is incomplete");
    }
    struct Pair {
        unsigned format;
        unsigned type;
        int bytes;
        const char* name;
    };
    const Pair pairs[] = {{glc::RGBA, glc::UNSIGNED_BYTE, 4, "RGBA/UNSIGNED_BYTE"}, {glc::BGRA, glc::UNSIGNED_BYTE, 4, "BGRA/UNSIGNED_BYTE"},
                          {glc::RGB, glc::UNSIGNED_BYTE, 3, "RGB/UNSIGNED_BYTE"},   {glc::RG, glc::UNSIGNED_BYTE, 2, "RG/UNSIGNED_BYTE"},
                          {glc::RED, glc::UNSIGNED_BYTE, 1, "RED/UNSIGNED_BYTE"},   {glc::RGBA, glc::UNSIGNED_SHORT, 8, "RGBA/UNSIGNED_SHORT"},
                          {glc::RGBA, glc::FLOAT, 16, "RGBA/FLOAT"},                {glc::RGBA, glc::UNSIGNED_INT_8_8_8_8_REV, 4, "RGBA/UNSIGNED_INT_8_8_8_8_REV"}};
    struct Store {
        int alignment;
        int row_length;
        int skip_pixels;
        int skip_rows;
    };
    const Store stores[] = {{1, 0, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0}, {1, 20, 0, 0}, {4, 24, 3, 2}, {2, 18, 1, 1}};
    constexpr int kW = 7;
    constexpr int kH = 5;
    Problems problems;
    int cases = 0;
    for (const Pair& pair : pairs) {
        for (const Store& store : stores) {
            const std::string text = std::string(pair.name) + " alignment " + std::to_string(store.alignment) + " row " + std::to_string(store.row_length) + " skip " +
                                     std::to_string(store.skip_pixels) + "," + std::to_string(store.skip_rows);
            const int row_pixels = store.row_length > 0 ? store.row_length : kW;
            int stride = row_pixels * pair.bytes;
            stride = (stride + store.alignment - 1) / store.alignment * store.alignment;
            const std::size_t total = static_cast<std::size_t>((store.skip_rows + kH + 1) * stride + store.skip_pixels * pair.bytes + 64);
            std::vector<std::uint8_t> client(total, 0xCD);
            api.glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo);
            api.glBindBuffer(glc::PIXEL_PACK_BUFFER, 0);
            api.glPixelStorei(glc::PACK_ALIGNMENT, store.alignment);
            api.glPixelStorei(glc::PACK_ROW_LENGTH, store.row_length);
            api.glPixelStorei(glc::PACK_SKIP_PIXELS, store.skip_pixels);
            api.glPixelStorei(glc::PACK_SKIP_ROWS, store.skip_rows);
            api.glReadPixels(2, 3, kW, kH, pair.format, pair.type, client.data());
            const unsigned pack_error = api.drain_errors();
            if (pack_error != 0) {
                problems.add(text + ": glReadPixels to client memory gave " + GlApi::error_name(pack_error));
                continue;
            }
            const unsigned pbo = make_buffer(api, total, std::vector<std::uint8_t>(total, 0xCD).data(), glc::STREAM_READ);
            api.glBindBuffer(glc::PIXEL_PACK_BUFFER, pbo);
            api.glReadPixels(2, 3, kW, kH, pair.format, pair.type, nullptr);
            const unsigned pbo_error = api.drain_errors();
            std::vector<std::uint8_t> from_pbo(total, 0);
            api.glGetBufferSubData(glc::PIXEL_PACK_BUFFER, 0, static_cast<std::ptrdiff_t>(total), from_pbo.data());
            api.glBindBuffer(glc::PIXEL_PACK_BUFFER, 0);
            api.glPixelStorei(glc::PACK_ALIGNMENT, 4);
            api.glPixelStorei(glc::PACK_ROW_LENGTH, 0);
            api.glPixelStorei(glc::PACK_SKIP_PIXELS, 0);
            api.glPixelStorei(glc::PACK_SKIP_ROWS, 0);
            if (pbo_error != 0 || !equivalent(pair.type, from_pbo, client)) {
                std::size_t at = 0;
                while (at < total && from_pbo[at] == client[at]) {
                    ++at;
                }
                problems.add(text + ": glReadPixels into a PBO gave " + GlApi::error_name(pbo_error) +
                             (equivalent(pair.type, from_pbo, client) ? std::string() : " and different bytes than the client-memory read, first at byte " + std::to_string(at) + " (PBO 0x" +
                                                                       Text::hex(from_pbo[at], 2) + ", client 0x" + Text::hex(client[at], 2) + ")"));
            }
            if (pair.format == glc::RGBA && pair.type == glc::UNSIGNED_BYTE && store.alignment == 1 && store.row_length == 0) {
                for (int y = 0; y < kH && problems.empty(); ++y) {
                    if (std::memcmp(client.data() + y * stride, source_pixels.data() + ((3 + y) * kSide + 2) * 4, kW * 4) != 0) {
                        problems.add(text + ": row " + std::to_string(y) + " of the read-back region differs from the uploaded image");
                    }
                }
            }
            std::vector<std::uint8_t> uploaded[2];
            for (int path = 0; path < 2; ++path) {
                unsigned destination = 0;
                api.glGenTextures(1, &destination);
                api.glBindTexture(glc::TEXTURE_2D, destination);
                api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
                const std::vector<std::uint8_t> zeros(kSide * kSide * 4, 0);
                api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
                api.glPixelStorei(glc::UNPACK_ROW_LENGTH, 0);
                api.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 0);
                api.glPixelStorei(glc::UNPACK_SKIP_ROWS, 0);
                api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), kSide, kSide, 0, glc::RGBA, glc::UNSIGNED_BYTE, zeros.data());
                api.glPixelStorei(glc::UNPACK_ALIGNMENT, store.alignment);
                api.glPixelStorei(glc::UNPACK_ROW_LENGTH, store.row_length);
                api.glPixelStorei(glc::UNPACK_SKIP_PIXELS, store.skip_pixels);
                api.glPixelStorei(glc::UNPACK_SKIP_ROWS, store.skip_rows);
                unsigned unpack_buffer = 0;
                if (path == 0) {
                    api.glTexSubImage2D(glc::TEXTURE_2D, 0, 4, 5, kW, kH, pair.format, pair.type, client.data());
                } else {
                    unpack_buffer = make_buffer(api, total, client.data(), glc::STATIC_DRAW);
                    api.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, unpack_buffer);
                    api.glTexSubImage2D(glc::TEXTURE_2D, 0, 4, 5, kW, kH, pair.format, pair.type, nullptr);
                    api.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, 0);
                }
                const unsigned unpack_error = api.drain_errors();
                api.glPixelStorei(glc::UNPACK_ALIGNMENT, 4);
                api.glPixelStorei(glc::UNPACK_ROW_LENGTH, 0);
                api.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 0);
                api.glPixelStorei(glc::UNPACK_SKIP_ROWS, 0);
                if (unpack_error != 0) {
                    problems.add(text + ": glTexSubImage2D from " + (path == 0 ? "client memory" : "a PBO") + " gave " + GlApi::error_name(unpack_error));
                }
                uploaded[path].assign(kSide * kSide * 4, 0xCD);
                api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
                api.glGetTexImage(glc::TEXTURE_2D, 0, glc::RGBA, glc::UNSIGNED_BYTE, uploaded[path].data());
                api.glPixelStorei(glc::PACK_ALIGNMENT, 4);
                api.glDeleteTextures(1, &destination);
                if (unpack_buffer != 0) {
                    api.glDeleteBuffers(1, &unpack_buffer);
                }
            }
            if (uploaded[0] != uploaded[1]) {
                problems.add(text + ": glTexSubImage2D from a PBO and from client memory produce different textures");
            }
            api.glDeleteBuffers(1, &pbo);
            ++cases;
            if (problems.empty() == false && cases > 4) {
                break;
            }
        }
    }
    api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    api.glDeleteFramebuffers(1, &fbo);
    api.glDeleteTextures(1, &texture);
    context.measure("cases", cases);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
