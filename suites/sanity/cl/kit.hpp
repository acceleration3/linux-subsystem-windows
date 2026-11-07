#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "core/text.hpp"
#include "support/artifact_writer.hpp"
#include "support/cl_kernel.hpp"
#include "support/cl_owned.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_sources.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace lsw::suites::sanity::cl {
class Kit {
public:
    using DeviceBody = std::function<void(ClRig&, Problems&)>;

    [[nodiscard]] static Outcome open_failed(const std::string& error) { return Outcome::fail("OpenCL: " + error); }

    [[nodiscard]] static Outcome each_device(Context& context, const DeviceBody& body, bool profiling = false);

    [[nodiscard]] static Outcome on_default(Context& context, const std::function<Outcome(ClRig&)>& body,
                                            bool profiling = false);

    static bool expect(Problems& problems, const std::string& what, cl_int got, std::initializer_list<cl_int> accepted);

    [[nodiscard]] static ClOwned<cl_mem> buffer(ClRig& rig, cl_mem_flags flags, std::size_t size, void* host,
                                                cl_int& code);

    [[nodiscard]] static ClOwned<cl_mem> buffer(ClRig& rig, cl_mem_flags flags, std::size_t size, Problems& problems,
                                                const std::string& what, void* host = nullptr);

    [[nodiscard]] static std::vector<std::uint32_t> words(std::uint64_t seed, std::size_t count);

    [[nodiscard]] static std::vector<std::uint8_t> bytes(std::uint64_t seed, std::size_t count);

    [[nodiscard]] static cl_int write(ClRig& rig, cl_mem mem, const void* data, std::size_t size, std::size_t offset = 0);

    [[nodiscard]] static cl_int read(ClRig& rig, cl_mem mem, void* data, std::size_t size, std::size_t offset = 0);

    [[nodiscard]] static bool read_words(ClRig& rig, cl_mem mem, std::size_t count, std::vector<std::uint32_t>& out,
                                         Problems& problems, const std::string& what);

    static bool same_bytes(Context& context, Problems& problems, const std::string& what, const void* expected,
                           const void* actual, std::size_t size);

    static bool same_words(Context& context, Problems& problems, const std::string& what,
                           const std::vector<std::uint32_t>& expected, const std::vector<std::uint32_t>& actual);

    [[nodiscard]] static bool compile(ClRig& rig, ClProgram& program, const std::string& source, const std::string& options,
                                      Problems& problems, const std::string& what);

    [[nodiscard]] static ClKernel kernel(ClProgram& program, const char* name, Problems& problems);

    struct Pair {
        std::vector<std::uint32_t> expected;
        std::vector<std::uint32_t> actual;
    };

    [[nodiscard]] static bool vector_add(ClRig& rig, std::size_t count, std::uint64_t seed, Pair& out, Problems& problems,
                                         const std::string& what);

    [[nodiscard]] static bool reduction(ClRig& rig, std::size_t count, std::size_t group, std::uint64_t seed, Pair& out,
                                        Problems& problems, const std::string& what);

    [[nodiscard]] static bool wait(ClRig& rig, cl_event event, Problems& problems, const std::string& what);

    [[nodiscard]] static cl_int event_status(ClRig& rig, cl_event event);

    [[nodiscard]] static std::string hex(std::uint64_t value, int digits = 8) { return Text::hex(value, digits); }

    [[nodiscard]] static std::string sanitize(std::string name);
};
}  // namespace lsw::suites::sanity::cl
