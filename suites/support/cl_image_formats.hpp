#pragma once

#include "support/cl_api.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
enum class ClReadClass { Float, Signed, Unsigned };

struct ClTexel {
    double value[4] = {0, 0, 0, 0};
    std::int64_t integer[4] = {0, 0, 0, 0};
};

class ClImageFormats {
public:
    [[nodiscard]] static int channels(cl_channel_order order);

    [[nodiscard]] static std::size_t element_size(const cl_image_format& format);

    [[nodiscard]] static bool known(const cl_image_format& format);

    [[nodiscard]] static ClReadClass read_class(cl_channel_type type);

    [[nodiscard]] static bool kernel_readable(const cl_image_format& format);

    [[nodiscard]] static bool decode(const cl_image_format& format, const std::uint8_t* texel, ClTexel& out);

    static void sanitize(const cl_image_format& format, std::uint8_t* data, std::size_t count);

    [[nodiscard]] static std::string name(const cl_image_format& format);

    [[nodiscard]] static std::string type_name(cl_mem_object_type type);

    [[nodiscard]] static float half_to_float(std::uint16_t half);

    [[nodiscard]] static std::vector<cl_image_format> required_read_only();

    [[nodiscard]] static std::vector<cl_image_format> required_read_write();
};
}  // namespace lsw::suites
