#include "support/cl_image_formats.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace lsw::suites {

namespace {

struct Named {
    cl_uint value;
    const char* name;
};

const Named kOrders[] = {
    {CL_R, "R"}, {CL_A, "A"}, {CL_RG, "RG"}, {CL_RA, "RA"}, {CL_RGB, "RGB"}, {CL_RGBA, "RGBA"}, {CL_BGRA, "BGRA"},
    {CL_ARGB, "ARGB"}, {CL_INTENSITY, "INTENSITY"}, {CL_LUMINANCE, "LUMINANCE"}, {CL_Rx, "Rx"}, {CL_RGx, "RGx"},
    {CL_RGBx, "RGBx"}, {CL_DEPTH, "DEPTH"}, {CL_sRGB, "sRGB"}, {CL_sRGBx, "sRGBx"},
    {CL_sRGBA, "sRGBA"}, {CL_sBGRA, "sBGRA"}, {CL_ABGR, "ABGR"},
};

const Named kTypes[] = {
    {CL_SNORM_INT8, "SNORM_INT8"}, {CL_SNORM_INT16, "SNORM_INT16"}, {CL_UNORM_INT8, "UNORM_INT8"},
    {CL_UNORM_INT16, "UNORM_INT16"}, {CL_UNORM_SHORT_565, "UNORM_SHORT_565"}, {CL_UNORM_SHORT_555, "UNORM_SHORT_555"},
    {CL_UNORM_INT_101010, "UNORM_INT_101010"}, {CL_SIGNED_INT8, "SIGNED_INT8"}, {CL_SIGNED_INT16, "SIGNED_INT16"},
    {CL_SIGNED_INT32, "SIGNED_INT32"}, {CL_UNSIGNED_INT8, "UNSIGNED_INT8"}, {CL_UNSIGNED_INT16, "UNSIGNED_INT16"},
    {CL_UNSIGNED_INT32, "UNSIGNED_INT32"}, {CL_HALF_FLOAT, "HALF_FLOAT"}, {CL_FLOAT, "FLOAT"},
    {CL_UNORM_INT_101010_2, "UNORM_INT_101010_2"},
};

bool is_packed(cl_channel_type type) {
    return type == CL_UNORM_SHORT_565 || type == CL_UNORM_SHORT_555 || type == CL_UNORM_INT_101010 ||
           type == CL_UNORM_INT_101010_2;
}

std::size_t type_bytes(cl_channel_type type) {
    switch (type) {
    case CL_SNORM_INT8:
    case CL_UNORM_INT8:
    case CL_SIGNED_INT8:
    case CL_UNSIGNED_INT8:
        return 1;
    case CL_SNORM_INT16:
    case CL_UNORM_INT16:
    case CL_SIGNED_INT16:
    case CL_UNSIGNED_INT16:
    case CL_HALF_FLOAT:
        return 2;
    case CL_SIGNED_INT32:
    case CL_UNSIGNED_INT32:
    case CL_FLOAT:
        return 4;
    default:
        return 0;
    }
}

std::uint64_t load(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 0;
    std::memcpy(&value, bytes, size);
    return value;
}

}  // namespace

int ClImageFormats::channels(cl_channel_order order) {
    switch (order) {
    case CL_R:
    case CL_A:
    case CL_INTENSITY:
    case CL_LUMINANCE:
    case CL_DEPTH:
        return 1;
    case CL_RG:
    case CL_RA:
    case CL_Rx:
        return 2;
    case CL_RGB:
    case CL_RGx:
    case CL_sRGB:
        return 3;
    case CL_RGBA:
    case CL_BGRA:
    case CL_ARGB:
    case CL_ABGR:
    case CL_RGBx:
    case CL_sRGBx:
    case CL_sRGBA:
    case CL_sBGRA:
        return 4;
    default:
        return -1;
    }
}

std::size_t ClImageFormats::element_size(const cl_image_format& format) {
    if (format.image_channel_data_type == CL_UNORM_SHORT_565 || format.image_channel_data_type == CL_UNORM_SHORT_555) {
        return 2;
    }
    if (format.image_channel_data_type == CL_UNORM_INT_101010 || format.image_channel_data_type == CL_UNORM_INT_101010_2) {
        return 4;
    }
    const int count = channels(format.image_channel_order);
    const std::size_t each = type_bytes(format.image_channel_data_type);
    return count > 0 && each != 0 ? static_cast<std::size_t>(count) * each : 0;
}

bool ClImageFormats::known(const cl_image_format& format) {
    return element_size(format) != 0;
}

ClReadClass ClImageFormats::read_class(cl_channel_type type) {
    switch (type) {
    case CL_SIGNED_INT8:
    case CL_SIGNED_INT16:
    case CL_SIGNED_INT32:
        return ClReadClass::Signed;
    case CL_UNSIGNED_INT8:
    case CL_UNSIGNED_INT16:
    case CL_UNSIGNED_INT32:
        return ClReadClass::Unsigned;
    default:
        return ClReadClass::Float;
    }
}

bool ClImageFormats::kernel_readable(const cl_image_format& format) {
    const cl_channel_order order = format.image_channel_order;
    if (!known(format) || order == CL_DEPTH || order == CL_sRGB || order == CL_sRGBx ||
        order == CL_sRGBA || order == CL_sBGRA || order == CL_ABGR) {
        return false;
    }
    return format.image_channel_data_type != CL_UNORM_INT_101010_2;
}

float ClImageFormats::half_to_float(std::uint16_t half) {
    const int sign = (half >> 15) & 1;
    const int exponent = (half >> 10) & 31;
    const int mantissa = half & 1023;
    float value;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa == 0 ? INFINITY : NAN;
    } else {
        value = std::ldexp(static_cast<float>(mantissa | 1024), exponent - 25);
    }
    return sign != 0 ? -value : value;
}

bool ClImageFormats::decode(const cl_image_format& format, const std::uint8_t* texel, ClTexel& out) {
    const cl_channel_type type = format.image_channel_data_type;
    double raw[4] = {0, 0, 0, 0};
    std::int64_t iraw[4] = {0, 0, 0, 0};
    int count = channels(format.image_channel_order);
    if (is_packed(type)) {
        const std::uint64_t v = load(texel, element_size(format));
        if (type == CL_UNORM_SHORT_565) {
            raw[0] = static_cast<double>((v >> 11) & 31) / 31.0;
            raw[1] = static_cast<double>((v >> 5) & 63) / 63.0;
            raw[2] = static_cast<double>(v & 31) / 31.0;
        } else if (type == CL_UNORM_SHORT_555) {
            raw[0] = static_cast<double>((v >> 10) & 31) / 31.0;
            raw[1] = static_cast<double>((v >> 5) & 31) / 31.0;
            raw[2] = static_cast<double>(v & 31) / 31.0;
        } else if (type == CL_UNORM_INT_101010) {
            raw[0] = static_cast<double>((v >> 20) & 1023) / 1023.0;
            raw[1] = static_cast<double>((v >> 10) & 1023) / 1023.0;
            raw[2] = static_cast<double>(v & 1023) / 1023.0;
        } else {
            return false;
        }
        count = 3;
    } else {
        const std::size_t each = type_bytes(type);
        for (int c = 0; c < count && c < 4; ++c) {
            const std::uint64_t bits = load(texel + static_cast<std::size_t>(c) * each, each);
            switch (type) {
            case CL_UNORM_INT8:
                raw[c] = static_cast<double>(bits) / 255.0;
                break;
            case CL_UNORM_INT16:
                raw[c] = static_cast<double>(bits) / 65535.0;
                break;
            case CL_SNORM_INT8:
                raw[c] = std::max(static_cast<double>(static_cast<std::int8_t>(bits)) / 127.0, -1.0);
                break;
            case CL_SNORM_INT16:
                raw[c] = std::max(static_cast<double>(static_cast<std::int16_t>(bits)) / 32767.0, -1.0);
                break;
            case CL_HALF_FLOAT:
                raw[c] = static_cast<double>(half_to_float(static_cast<std::uint16_t>(bits)));
                break;
            case CL_FLOAT: {
                std::uint32_t b32 = static_cast<std::uint32_t>(bits);
                float f;
                std::memcpy(&f, &b32, 4);
                raw[c] = static_cast<double>(f);
                break;
            }
            case CL_SIGNED_INT8:
                iraw[c] = static_cast<std::int8_t>(bits);
                break;
            case CL_SIGNED_INT16:
                iraw[c] = static_cast<std::int16_t>(bits);
                break;
            case CL_SIGNED_INT32:
                iraw[c] = static_cast<std::int32_t>(bits);
                break;
            case CL_UNSIGNED_INT8:
            case CL_UNSIGNED_INT16:
            case CL_UNSIGNED_INT32:
                iraw[c] = static_cast<std::int64_t>(bits);
                break;
            default:
                return false;
            }
        }
    }
    auto set = [&](int slot, int source) {
        out.value[slot] = raw[source];
        out.integer[slot] = iraw[source];
    };
    auto fixed = [&](int slot, double v, std::int64_t i) {
        out.value[slot] = v;
        out.integer[slot] = i;
    };
    switch (format.image_channel_order) {
    case CL_R:
    case CL_DEPTH:
        set(0, 0);
        fixed(1, 0, 0);
        fixed(2, 0, 0);
        fixed(3, 1, 1);
        break;
    case CL_A:
        fixed(0, 0, 0);
        fixed(1, 0, 0);
        fixed(2, 0, 0);
        set(3, 0);
        break;
    case CL_RG:
    case CL_RGx:
        set(0, 0);
        set(1, 1);
        fixed(2, 0, 0);
        fixed(3, 1, 1);
        break;
    case CL_RA:
        set(0, 0);
        fixed(1, 0, 0);
        fixed(2, 0, 0);
        set(3, 1);
        break;
    case CL_Rx:
        set(0, 0);
        fixed(1, 0, 0);
        fixed(2, 0, 0);
        fixed(3, 1, 1);
        break;
    case CL_RGB:
        set(0, 0);
        set(1, 1);
        set(2, 2);
        fixed(3, 1, 1);
        break;
    case CL_RGBx:
        set(0, 0);
        set(1, 1);
        set(2, 2);
        fixed(3, 1, 1);
        break;
    case CL_RGBA:
        for (int c = 0; c < 4; ++c) {
            set(c, c);
        }
        break;
    case CL_BGRA:
        set(0, 2);
        set(1, 1);
        set(2, 0);
        set(3, 3);
        break;
    case CL_ARGB:
        set(0, 1);
        set(1, 2);
        set(2, 3);
        set(3, 0);
        break;
    case CL_INTENSITY:
        for (int c = 0; c < 4; ++c) {
            set(c, 0);
        }
        break;
    case CL_LUMINANCE:
        set(0, 0);
        set(1, 0);
        set(2, 0);
        fixed(3, 1, 1);
        break;
    default:
        return false;
    }
    return true;
}

void ClImageFormats::sanitize(const cl_image_format& format, std::uint8_t* data, std::size_t count) {
    if (format.image_channel_data_type == CL_HALF_FLOAT) {
        for (std::size_t i = 0; i + 1 < count; i += 2) {
            std::uint16_t v;
            std::memcpy(&v, data + i, 2);
            if ((v & 0x7C00u) == 0x7C00u) {
                v = static_cast<std::uint16_t>(v & 0xBFFFu);
            }
            if ((v & 0x7C00u) == 0) {
                v = static_cast<std::uint16_t>(v | 0x0400u);
            }
            std::memcpy(data + i, &v, 2);
        }
    } else if (format.image_channel_data_type == CL_FLOAT) {
        for (std::size_t i = 0; i + 3 < count; i += 4) {
            std::uint32_t v;
            std::memcpy(&v, data + i, 4);
            if ((v & 0x7F800000u) == 0x7F800000u) {
                v &= 0xBFFFFFFFu;
            }
            if ((v & 0x7F800000u) == 0) {
                v |= 0x00800000u;
            }
            std::memcpy(data + i, &v, 4);
        }
    }
}

std::string ClImageFormats::name(const cl_image_format& format) {
    std::string order = "order " + std::to_string(format.image_channel_order);
    std::string type = "type " + std::to_string(format.image_channel_data_type);
    for (const Named& named : kOrders) {
        if (named.value == format.image_channel_order) {
            order = std::string("CL_") + named.name;
        }
    }
    for (const Named& named : kTypes) {
        if (named.value == format.image_channel_data_type) {
            type = std::string("CL_") + named.name;
        }
    }
    return order + "/" + type;
}

std::string ClImageFormats::type_name(cl_mem_object_type type) {
    switch (type) {
    case CL_MEM_OBJECT_IMAGE1D:
        return "image1d";
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
        return "image1d_buffer";
    case CL_MEM_OBJECT_IMAGE1D_ARRAY:
        return "image1d_array";
    case CL_MEM_OBJECT_IMAGE2D:
        return "image2d";
    case CL_MEM_OBJECT_IMAGE2D_ARRAY:
        return "image2d_array";
    case CL_MEM_OBJECT_IMAGE3D:
        return "image3d";
    default:
        return "image(0x" + std::to_string(type) + ")";
    }
}

std::vector<cl_image_format> ClImageFormats::required_read_only() {
    std::vector<cl_image_format> list;
    for (const cl_channel_type type : std::initializer_list<cl_channel_type>{CL_UNORM_INT8, CL_UNORM_INT16, CL_SIGNED_INT8, CL_SIGNED_INT16, CL_SIGNED_INT32,
                                       CL_UNSIGNED_INT8, CL_UNSIGNED_INT16, CL_UNSIGNED_INT32, CL_HALF_FLOAT, CL_FLOAT}) {
        list.push_back({CL_RGBA, type});
    }
    list.push_back({CL_BGRA, CL_UNORM_INT8});
    return list;
}

std::vector<cl_image_format> ClImageFormats::required_read_write() {
    std::vector<cl_image_format> list;
    for (const cl_channel_type type : std::initializer_list<cl_channel_type>{CL_FLOAT, CL_UNSIGNED_INT32, CL_SIGNED_INT32}) {
        list.push_back({CL_R, type});
    }
    for (const cl_channel_type type : std::initializer_list<cl_channel_type>{CL_UNORM_INT8, CL_UNSIGNED_INT8, CL_SIGNED_INT8, CL_UNSIGNED_INT16, CL_SIGNED_INT16,
                                       CL_UNSIGNED_INT32, CL_SIGNED_INT32, CL_HALF_FLOAT, CL_FLOAT}) {
        list.push_back({CL_RGBA, type});
    }
    return list;
}

}  // namespace lsw::suites
