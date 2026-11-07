#pragma once

#include "sanity/cl/kit.hpp"
#include "support/cl_image_formats.hpp"

#include <array>

namespace lsw::suites::sanity::cl {
struct ImageShape {
    cl_mem_object_type type = CL_MEM_OBJECT_IMAGE2D;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t depth = 0;
    std::size_t array = 0;

    [[nodiscard]] std::array<std::size_t, 3> region() const;

    [[nodiscard]] std::array<std::size_t, 3> dims() const;

    [[nodiscard]] std::size_t pixels() const;

    [[nodiscard]] std::string name() const { return ClImageFormats::type_name(type); }

    [[nodiscard]] const char* kernel_image() const;

    [[nodiscard]] bool is_array() const {
        return type == CL_MEM_OBJECT_IMAGE1D_ARRAY || type == CL_MEM_OBJECT_IMAGE2D_ARRAY;
    }
};

class ImageKit {
public:
    [[nodiscard]] static std::vector<ImageShape> shapes(const ClRig& rig, bool include_buffer);

    [[nodiscard]] static cl_image_desc descriptor(const ImageShape& shape, cl_mem buffer = nullptr);

    [[nodiscard]] static ClOwned<cl_mem> create(ClRig& rig, cl_mem_flags flags, const cl_image_format& format,
                                                const ImageShape& shape, void* host, cl_int& code,
                                                ClOwned<cl_mem>* backing = nullptr);

    [[nodiscard]] static std::string kernel_read_source(const std::vector<ImageShape>& shapes);

    [[nodiscard]] static std::string read_kernel_name(const ImageShape& shape, ClReadClass read_class);
};
}  // namespace lsw::suites::sanity::cl
