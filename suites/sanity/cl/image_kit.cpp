#include "sanity/cl/image_kit.hpp"

namespace lsw::suites::sanity::cl {

std::array<std::size_t, 3> ImageShape::region() const {
    switch (type) {
    case CL_MEM_OBJECT_IMAGE1D:
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
        return {width, 1, 1};
    case CL_MEM_OBJECT_IMAGE1D_ARRAY:
        return {width, array, 1};
    case CL_MEM_OBJECT_IMAGE2D:
        return {width, height, 1};
    case CL_MEM_OBJECT_IMAGE2D_ARRAY:
        return {width, height, array};
    default:
        return {width, height, depth};
    }
}

std::array<std::size_t, 3> ImageShape::dims() const {
    return region();
}

std::size_t ImageShape::pixels() const {
    const auto r = region();
    return r[0] * r[1] * r[2];
}

const char* ImageShape::kernel_image() const {
    switch (type) {
    case CL_MEM_OBJECT_IMAGE1D:
        return "image1d_t";
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
        return "image1d_buffer_t";
    case CL_MEM_OBJECT_IMAGE1D_ARRAY:
        return "image1d_array_t";
    case CL_MEM_OBJECT_IMAGE2D:
        return "image2d_t";
    case CL_MEM_OBJECT_IMAGE2D_ARRAY:
        return "image2d_array_t";
    default:
        return "image3d_t";
    }
}

std::vector<ImageShape> ImageKit::shapes(const ClRig& rig, bool include_buffer) {
    std::vector<ImageShape> list;
    if (rig.version.at_least(1, 2)) {
        list.push_back({CL_MEM_OBJECT_IMAGE1D, 16, 0, 0, 0});
        if (include_buffer) {
            list.push_back({CL_MEM_OBJECT_IMAGE1D_BUFFER, 16, 0, 0, 0});
        }
        list.push_back({CL_MEM_OBJECT_IMAGE1D_ARRAY, 16, 0, 0, 3});
    }
    list.push_back({CL_MEM_OBJECT_IMAGE2D, 16, 8, 0, 0});
    if (rig.version.at_least(1, 2)) {
        list.push_back({CL_MEM_OBJECT_IMAGE2D_ARRAY, 8, 4, 0, 3});
    }
    list.push_back({CL_MEM_OBJECT_IMAGE3D, 8, 4, 3, 0});
    return list;
}

cl_image_desc ImageKit::descriptor(const ImageShape& shape, cl_mem buffer) {
    cl_image_desc desc{};
    desc.image_type = shape.type;
    desc.image_width = shape.width;
    desc.image_height = shape.height;
    desc.image_depth = shape.depth;
    desc.image_array_size = shape.array;
    desc.mem_object = buffer;
    return desc;
}

ClOwned<cl_mem> ImageKit::create(ClRig& rig, cl_mem_flags flags, const cl_image_format& format, const ImageShape& shape,
                                 void* host, cl_int& code, ClOwned<cl_mem>* backing) {
    cl_mem buffer = nullptr;
    if (shape.type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
        const std::size_t bytes = shape.width * ClImageFormats::element_size(format);
        ClOwned<cl_mem> made = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
        if (!made) {
            return {};
        }
        buffer = made.get();
        if (backing != nullptr) {
            *backing = std::move(made);
        } else {
            return {};
        }
    }
    const cl_image_desc desc = descriptor(shape, buffer);
    cl_mem image = rig.api.CreateImage(rig.context, flags, &format, &desc, host, &code);
    if (code != CL_SUCCESS) {
        if (image != nullptr) {
            rig.api.ReleaseMemObject(image);
        }
        return {};
    }
    return ClOwned<cl_mem>(rig, image);
}

std::string ImageKit::read_kernel_name(const ImageShape& shape, ClReadClass read_class) {
    const char* suffix = read_class == ClReadClass::Float ? "f" : read_class == ClReadClass::Signed ? "i" : "ui";
    return std::string("rd_") + shape.kernel_image() + "_" + suffix;
}

std::string ImageKit::kernel_read_source(const std::vector<ImageShape>& shapes) {
    std::string source;
    const struct {
        ClReadClass read_class;
        const char* vector;
        const char* function;
    } classes[] = {{ClReadClass::Float, "float4", "read_imagef"},
                   {ClReadClass::Signed, "int4", "read_imagei"},
                   {ClReadClass::Unsigned, "uint4", "read_imageui"}};
    for (const ImageShape& shape : shapes) {
        std::string coord;
        switch (shape.type) {
        case CL_MEM_OBJECT_IMAGE1D:
        case CL_MEM_OBJECT_IMAGE1D_BUFFER:
            coord = "(int)x";
            break;
        case CL_MEM_OBJECT_IMAGE1D_ARRAY:
        case CL_MEM_OBJECT_IMAGE2D:
            coord = "(int2)((int)x, (int)y)";
            break;
        default:
            coord = "(int4)((int)x, (int)y, (int)z, 0)";
            break;
        }
        for (const auto& c : classes) {
            source += "__kernel void " + read_kernel_name(shape, c.read_class) + "(__read_only " + shape.kernel_image() +
                      " img, __global " + c.vector + "* out) {\n" +
                      "    const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST;\n" +
                      "    size_t x = get_global_id(0), y = get_global_id(1), z = get_global_id(2);\n" +
                      "    size_t w = get_global_size(0), h = get_global_size(1);\n" +
                      "    out[x + w * (y + h * z)] = " + c.function +
                      (shape.type == CL_MEM_OBJECT_IMAGE1D_BUFFER ? "(img, " + coord + ");\n}\n" : "(img, smp, " + coord + ");\n}\n");
        }
    }
    return source;
}

}  // namespace lsw::suites::sanity::cl
