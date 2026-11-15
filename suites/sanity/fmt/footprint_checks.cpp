#include "sanity/fmt/footprint_checks.hpp"

#include "sanity/cl/image_kit.hpp"
#include "sanity/fmt/format_data.hpp"
#include "sanity/fmt/gl_format_io.hpp"
#include "sanity/fmt/kit.hpp"
#include "sanity/fmt/vulkan_format_io.hpp"
#include "sanity/gl/kit.hpp"
#include "sanity/vk/kit.hpp"
#include "support/d3d12_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/format_runner.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr unsigned kWidth = 37;
constexpr unsigned kHeight = 19;
constexpr unsigned kMips = 6;
constexpr unsigned kLayers = 3;

class Limited {
public:
    explicit Limited(Problems& target) : problems_(target) {}

    void add(const std::string& text) {
        if (++count_ <= 30) {
            problems_.add(text);
        }
    }

private:
    Problems& problems_;
    int count_ = 0;
};

unsigned dim(unsigned value, unsigned mip) { return std::max(1u, value >> mip); }

void expected_layout(DXGI_FORMAT format, unsigned width, unsigned height, unsigned& row_bytes, unsigned& rows) {
    FormatRunner::layout(format, width, height, row_bytes, rows);
}

void d3d11_part(Context& context, Limited& out) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error)) {
        out.add("D3D11: " + error);
        return;
    }
    int tested = 0;
    for (const FormatEntry& entry : FormatTable::all()) {
        UINT bits = 0;
        if (FAILED(device.device->CheckFormatSupport(entry.dxgi, &bits)) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0 || (bits & D3D11_FORMAT_SUPPORT_MIP) == 0) {
            continue;
        }
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> texture =
            D3d11Tools::texture(device, D3d11Tools::desc2d(kWidth, kHeight, entry.dxgi, 0, D3D11_USAGE_DEFAULT, 0, kMips, kLayers), nullptr, &result);
        const std::string name = std::string("D3D11 ") + entry.name;
        if (!texture) {
            out.add(name + " reports Texture2D and mip support but a " + std::to_string(kMips) + " mip, " + std::to_string(kLayers) + " layer texture cannot be created: " +
                    D3d11Tools::hr(result));
            continue;
        }
        ++tested;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.MipLevels != kMips || desc.ArraySize != kLayers) {
            out.add(name + " reports " + std::to_string(desc.MipLevels) + " mips and " + std::to_string(desc.ArraySize) + " layers after creation");
        }
        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> staging;
        if (FAILED(device.device->CreateTexture2D(&staging_desc, nullptr, staging.put()))) {
            out.add(name + " has no staging twin");
            continue;
        }
        for (unsigned layer = 0; layer < kLayers; ++layer) {
            for (unsigned mip = 0; mip < kMips; ++mip) {
                const unsigned subresource = D3D11CalcSubresource(mip, layer, kMips);
                unsigned row_bytes = 0;
                unsigned rows = 0;
                expected_layout(entry.dxgi, dim(kWidth, mip), dim(kHeight, mip), row_bytes, rows);
                const Bytes data = FormatData::make(entry.dxgi, 0x1100 + subresource, dim(kWidth, mip), dim(kHeight, mip));
                device.context->UpdateSubresource(texture.get(), subresource, nullptr, data.data(), row_bytes, 0);
                device.context->CopySubresourceRegion(staging.get(), subresource, 0, 0, 0, texture.get(), subresource, nullptr);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(device.context->Map(staging.get(), subresource, D3D11_MAP_READ, 0, &mapped))) {
                    out.add(name + " subresource " + std::to_string(subresource) + " cannot be mapped");
                    continue;
                }
                if (mapped.RowPitch < row_bytes) {
                    out.add(name + " mip " + std::to_string(mip) + " layer " + std::to_string(layer) + " has a row pitch of " + std::to_string(mapped.RowPitch) +
                            " for rows of " + std::to_string(row_bytes) + " bytes");
                } else {
                    const Bytes got = D3d11Tools::block_bytes(mapped.pData, row_bytes, rows, mapped.RowPitch);
                    if (got != data) {
                        out.add(name + " mip " + std::to_string(mip) + " layer " + std::to_string(layer) + " does not read back what was written at byte " +
                                std::to_string(Kit::first_difference(data, got)));
                    }
                }
                device.context->Unmap(staging.get(), subresource);
            }
        }
    }
    context.measure("fmt.footprint.d3d11.formats", tested);
}

void d3d12_part(Context& context, Limited& out) {
    D3d12Rig rig;
    std::string error;
    if (!Kit::open_d3d12(rig, error)) {
        out.add("D3D12: " + error);
        return;
    }
    int tested = 0;
    for (const FormatEntry& entry : FormatTable::all()) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = entry.dxgi;
        if (FAILED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
            (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) == 0 || (support.Support1 & D3D12_FORMAT_SUPPORT1_MIP) == 0) {
            continue;
        }
        const std::string name = std::string("D3D12 ") + entry.name;
        HRESULT result = S_OK;
        ID3D12Resource* texture =
            D3d12Tools::texture(rig, D3d12Tools::texture_desc(kWidth, kHeight, entry.dxgi, D3D12_RESOURCE_FLAG_NONE, kMips, kLayers), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
        if (texture == nullptr) {
            out.add(name + " reports Texture2D and mip support but a mipped array texture cannot be created: " + D3d12Tools::hr(result));
            continue;
        }
        ++tested;
        const D3D12_RESOURCE_DESC desc = texture->GetDesc();
        const UINT count = kMips * kLayers;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(count);
        std::vector<UINT> rows(count);
        std::vector<UINT64> row_sizes(count);
        UINT64 total = 0;
        rig.device()->GetCopyableFootprints(&desc, 0, count, 0, footprints.data(), rows.data(), row_sizes.data(), &total);
        UINT64 previous_end = 0;
        for (UINT sub = 0; sub < count; ++sub) {
            const unsigned mip = sub % kMips;
            unsigned row_bytes = 0;
            unsigned want_rows = 0;
            expected_layout(entry.dxgi, dim(kWidth, mip), dim(kHeight, mip), row_bytes, want_rows);
            const std::string level = name + " subresource " + std::to_string(sub) + " (mip " + std::to_string(mip) + ")";
            if (rows[sub] != want_rows || row_sizes[sub] != row_bytes) {
                out.add(level + " has " + std::to_string(rows[sub]) + " rows of " + std::to_string(row_sizes[sub]) + " bytes, expected " + std::to_string(want_rows) +
                        " rows of " + std::to_string(row_bytes));
            }
            if (footprints[sub].Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0 || footprints[sub].Footprint.RowPitch < row_sizes[sub]) {
                out.add(level + " has a row pitch of " + std::to_string(footprints[sub].Footprint.RowPitch) + ", not a 256 byte multiple of at least the row size");
            }
            if (footprints[sub].Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0 || footprints[sub].Offset < previous_end) {
                out.add(level + " is placed at offset " + std::to_string(footprints[sub].Offset) + ", not 512 aligned or overlapping the previous subresource");
            }
            previous_end = footprints[sub].Offset + static_cast<UINT64>(footprints[sub].Footprint.RowPitch) * (rows[sub] - 1) + row_sizes[sub];
        }
        if (total < previous_end) {
            out.add(name + " reports a total of " + std::to_string(total) + " bytes, less than the end of the last subresource at " + std::to_string(previous_end));
        }
    }
    context.measure("fmt.footprint.d3d12.formats", tested);
}

void d3d9_part(Context& context, Limited& out) {
    D3d9Rig rig;
    std::string error;
    if (!rig.open(false, error) || !rig.create(D3d9Settings{}, error)) {
        out.add("D3D9: " + error);
        return;
    }
    int tested = 0;
    for (const FormatEntry& entry : FormatTable::all()) {
        if (entry.d3d9 == 0 || FormatCodec::is_block(entry.dxgi)) {
            continue;
        }
        const D3DFORMAT format = static_cast<D3DFORMAT>(entry.d3d9);
        if (rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, format) != D3D_OK) {
            continue;
        }
        const std::string name = std::string("D3D9 ") + entry.name;
        HRESULT result = S_OK;
        Com<IDirect3DTexture9> texture = D3d9Tools::texture(rig.dev(), kWidth, kHeight, 0, 0, format, D3DPOOL_MANAGED, &result);
        if (!texture) {
            out.add(name + " is reported as a texture format but a managed texture cannot be created: " + D3d9Tools::hr(result));
            continue;
        }
        ++tested;
        const unsigned levels = static_cast<unsigned>(texture->GetLevelCount());
        unsigned want_levels = 1;
        for (unsigned largest = std::max(kWidth, kHeight); largest > 1; largest >>= 1) {
            ++want_levels;
        }
        if (levels != want_levels) {
            out.add(name + " has " + std::to_string(levels) + " levels, a full chain has " + std::to_string(want_levels));
        }
        for (unsigned m = 0; m < levels; ++m) {
            D3DSURFACE_DESC desc{};
            texture->GetLevelDesc(m, &desc);
            if (desc.Width != dim(kWidth, m) || desc.Height != dim(kHeight, m)) {
                out.add(name + " level " + std::to_string(m) + " is " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + ", expected " +
                        std::to_string(dim(kWidth, m)) + "x" + std::to_string(dim(kHeight, m)));
            }
            unsigned row_bytes = 0;
            unsigned rows = 0;
            expected_layout(entry.dxgi, dim(kWidth, m), dim(kHeight, m), row_bytes, rows);
            D3DLOCKED_RECT locked{};
            if (FAILED(texture->LockRect(m, &locked, nullptr, 0))) {
                out.add(name + " level " + std::to_string(m) + " cannot be locked");
                continue;
            }
            if (locked.Pitch < static_cast<INT>(row_bytes)) {
                out.add(name + " level " + std::to_string(m) + " has a pitch of " + std::to_string(locked.Pitch) + " for rows of " + std::to_string(row_bytes) + " bytes");
            } else {
                const Bytes data = FormatData::make(entry.dxgi, 0x1200 + m, dim(kWidth, m), dim(kHeight, m));
                for (unsigned row = 0; row < rows; ++row) {
                    std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.Pitch),
                                data.data() + static_cast<std::size_t>(row) * row_bytes, row_bytes);
                }
            }
            texture->UnlockRect(m);
            if (SUCCEEDED(texture->LockRect(m, &locked, nullptr, D3DLOCK_READONLY))) {
                const Bytes data = FormatData::make(entry.dxgi, 0x1200 + m, dim(kWidth, m), dim(kHeight, m));
                for (unsigned row = 0; row < rows; ++row) {
                    if (std::memcmp(static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.Pitch),
                                    data.data() + static_cast<std::size_t>(row) * row_bytes, row_bytes) != 0) {
                        out.add(name + " level " + std::to_string(m) + " row " + std::to_string(row) + " does not hold what was written");
                        break;
                    }
                }
                texture->UnlockRect(m);
            }
        }
    }
    context.measure("fmt.footprint.d3d9.formats", tested);
}

Outcome vulkan_part(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        Limited out(problems);
        int tested = 0;
        for (const FormatEntry& entry : FormatTable::all()) {
            if (entry.vulkan == 0) {
                continue;
            }
            const VkFormat format = static_cast<VkFormat>(entry.vulkan);
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VkImageFormatProperties props{};
            if (rig.fn.vkGetPhysicalDeviceImageFormatProperties(rig.physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &props) != VK_SUCCESS ||
                props.maxMipLevels < kMips || props.maxArrayLayers < kLayers) {
                continue;
            }
            ++tested;
            const std::string name = std::string("Vulkan ") + entry.name;
            VulkanImage image;
            VkResult result = VulkanFormatIo::make(rig, image, format, kWidth, kHeight, usage, kMips, kLayers);
            if (result != VK_SUCCESS) {
                out.add(name + " cannot be created with " + std::to_string(kMips) + " mips and " + std::to_string(kLayers) + " layers: " + vk::Kit::name(result));
                continue;
            }
            for (unsigned layer = 0; layer < kLayers; ++layer) {
                for (unsigned mip = 0; mip < kMips; ++mip) {
                    const Bytes data = FormatData::make(entry.dxgi, 0x1300 + layer * 16 + mip, dim(kWidth, mip), dim(kHeight, mip));
                    Bytes back;
                    result = VulkanFormatIo::upload(rig, image, data, dim(kWidth, mip), dim(kHeight, mip), mip, layer);
                    if (result == VK_SUCCESS) {
                        result = VulkanFormatIo::download(rig, image, data.size(), dim(kWidth, mip), dim(kHeight, mip), back, mip, layer);
                    }
                    if (result != VK_SUCCESS) {
                        out.add(name + " mip " + std::to_string(mip) + " layer " + std::to_string(layer) + " transfer failed: " + vk::Kit::name(result));
                    } else if (back != data) {
                        out.add(name + " mip " + std::to_string(mip) + " layer " + std::to_string(layer) + " does not read back what was written at byte " +
                                std::to_string(Kit::first_difference(data, back)));
                    }
                }
            }
            for (unsigned layer = 0; layer < kLayers; ++layer) {
                const Bytes data = FormatData::make(entry.dxgi, 0x1300 + layer * 16, dim(kWidth, 0), dim(kHeight, 0));
                Bytes back;
                if (VulkanFormatIo::download(rig, image, data.size(), dim(kWidth, 0), dim(kHeight, 0), back, 0, layer) == VK_SUCCESS && back != data) {
                    out.add(name + " layer " + std::to_string(layer) + " was changed by writes to other layers or mips");
                }
            }
        }
        context.measure("fmt.footprint.vulkan.formats", tested);
        return {};
    });
}

void gl_part(Context& context, Limited& out) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        out.add("OpenGL: " + error);
        return;
    }
    const GlApi& api = rig.gl;
    int tested = 0;
    for (const FormatEntry& entry : FormatTable::all()) {
        if (entry.gl_internal == 0 || entry.gl_format == 0 || !GlFormatIo::reported_supported(api, entry.gl_internal)) {
            continue;
        }
        const std::string name = std::string("OpenGL ") + entry.name;
        api.drain_errors();
        unsigned texture = 0;
        api.glGenTextures(1, &texture);
        api.glBindTexture(glc::TEXTURE_2D, texture);
        api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
        bool created = true;
        for (unsigned mip = 0; mip < kMips && created; ++mip) {
            const Bytes data = FormatData::make(entry.dxgi, 0x1400 + mip, dim(kWidth, mip), dim(kHeight, mip));
            api.glTexImage2D(glc::TEXTURE_2D, static_cast<int>(mip), static_cast<int>(entry.gl_internal), static_cast<int>(dim(kWidth, mip)),
                             static_cast<int>(dim(kHeight, mip)), 0, entry.gl_format, entry.gl_type, data.data());
            if (api.glGetError() != 0) {
                created = false;
                break;
            }
            int width = 0;
            int height = 0;
            api.glGetTexLevelParameteriv(glc::TEXTURE_2D, static_cast<int>(mip), glc::TEXTURE_WIDTH, &width);
            api.glGetTexLevelParameteriv(glc::TEXTURE_2D, static_cast<int>(mip), glc::TEXTURE_HEIGHT, &height);
            if (static_cast<unsigned>(width) != dim(kWidth, mip) || static_cast<unsigned>(height) != dim(kHeight, mip)) {
                out.add(name + " level " + std::to_string(mip) + " is " + std::to_string(width) + "x" + std::to_string(height) + ", expected " +
                        std::to_string(dim(kWidth, mip)) + "x" + std::to_string(dim(kHeight, mip)));
            }
            Bytes back(data.size(), 0xCD);
            api.glGetTexImage(glc::TEXTURE_2D, static_cast<int>(mip), entry.gl_format, entry.gl_type, back.data());
            if (api.glGetError() != 0 || back != data) {
                out.add(name + " level " + std::to_string(mip) + " does not read back what was uploaded");
            }
        }
        if (created) {
            ++tested;
        }
        api.glFinish();
        api.glBindTexture(glc::TEXTURE_2D, 0);
        api.glDeleteTextures(1, &texture);
    }
    context.measure("fmt.footprint.gl.formats", tested);
}

Outcome opencl_part(Context& context) {
    return cl::Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        if (!rig.images() || !rig.version.at_least(1, 2)) {
            return;
        }
        Limited out(problems);
        cl_uint count = 0;
        if (rig.api.GetSupportedImageFormats(rig.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D_ARRAY, 0, nullptr, &count) != CL_SUCCESS) {
            return;
        }
        std::vector<cl_image_format> formats(count);
        if (count != 0) {
            rig.api.GetSupportedImageFormats(rig.context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D_ARRAY, count, formats.data(), nullptr);
        }
        int tested = 0;
        for (const cl_image_format& format : formats) {
            const std::size_t element = ClImageFormats::element_size(format);
            if (element == 0 || !ClImageFormats::known(format)) {
                continue;
            }
            ++tested;
            cl::ImageShape shape;
            shape.type = CL_MEM_OBJECT_IMAGE2D_ARRAY;
            shape.width = kWidth;
            shape.height = kHeight;
            shape.array = kLayers;
            const std::string name = "OpenCL " + ClImageFormats::name(format);
            Bytes data = cl::Kit::bytes(0x1500 + static_cast<std::uint64_t>(format.image_channel_order), element * kWidth * kHeight * kLayers);
            ClImageFormats::sanitize(format, data.data(), data.size());
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> image = cl::ImageKit::create(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, format, shape, data.data(), code);
            if (!image) {
                out.add(name + " is listed for 2D arrays but creation failed with " + ClTools::error_name(code));
                continue;
            }
            std::size_t row_pitch = 0;
            std::size_t slice_pitch = 0;
            rig.api.GetImageInfo(image.get(), CL_IMAGE_ROW_PITCH, sizeof(row_pitch), &row_pitch, nullptr);
            rig.api.GetImageInfo(image.get(), CL_IMAGE_SLICE_PITCH, sizeof(slice_pitch), &slice_pitch, nullptr);
            if (row_pitch < kWidth * element) {
                out.add(name + " reports a row pitch of " + std::to_string(row_pitch) + " for rows of " + std::to_string(kWidth * element) + " bytes");
            }
            if (slice_pitch < row_pitch * kHeight) {
                out.add(name + " reports a slice pitch of " + std::to_string(slice_pitch) + ", less than " + std::to_string(row_pitch * kHeight) + " bytes");
            }
            Bytes back(data.size(), 0xCD);
            const std::size_t origin[3] = {0, 0, 0};
            const std::size_t region[3] = {kWidth, kHeight, kLayers};
            code = rig.api.EnqueueReadImage(rig.queue, image.get(), CL_TRUE, origin, region, 0, 0, back.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                out.add(name + " readback failed with " + ClTools::error_name(code));
            } else if (back != data) {
                out.add(name + " does not read back what it was created from at byte " + std::to_string(Kit::first_difference(data, back)));
            }
        }
        context.measure("fmt.footprint.opencl.formats", tested);
    });
}

void merge(const char* api, const Outcome& outcome, Problems& problems, std::vector<std::string>& skips, bool& exercised) {
    if (outcome.status() == Status::Fail) {
        problems.add(std::string(api) + ": " + outcome.reason());
    } else if (outcome.status() == Status::Skip) {
        skips.push_back(std::string(api) + ": " + outcome.reason());
    } else {
        exercised = true;
    }
}

}  // namespace

Outcome FootprintChecks::mip_and_array_footprints(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    Limited out(problems);
    if (Kit::enabled("d3d11")) {
        d3d11_part(context, out);
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        d3d12_part(context, out);
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("d3d9")) {
        d3d9_part(context, out);
        exercised = true;
    } else {
        skips.push_back("D3D9 excluded");
    }
    if (Kit::enabled("vulkan")) {
        merge("Vulkan", vulkan_part(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        gl_part(context, out);
        exercised = true;
    } else {
        skips.push_back("OpenGL excluded");
    }
    if (Kit::enabled("opencl")) {
        merge("OpenCL", opencl_part(context), problems, skips, exercised);
    } else {
        skips.push_back("OpenCL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
