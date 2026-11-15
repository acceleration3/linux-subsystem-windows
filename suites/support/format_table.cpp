#include "support/format_table.hpp"

#include "support/format_codec.hpp"
#include "support/vulkan_formats.hpp"

#include <d3d9.h>

namespace lsw::suites {

namespace {

constexpr unsigned kUByte = 0x1401;
constexpr unsigned kByte = 0x1400;
constexpr unsigned kUShort = 0x1403;
constexpr unsigned kShort = 0x1402;
constexpr unsigned kUInt = 0x1405;
constexpr unsigned kInt = 0x1404;
constexpr unsigned kFloat = 0x1406;
constexpr unsigned kHalf = 0x140B;
constexpr unsigned kRed = 0x1903;
constexpr unsigned kRg = 0x8227;
constexpr unsigned kRgb = 0x1907;
constexpr unsigned kRgba = 0x1908;
constexpr unsigned kBgra = 0x80E1;
constexpr unsigned kRedInteger = 0x8D94;
constexpr unsigned kRgInteger = 0x8228;
constexpr unsigned kRgbInteger = 0x8D98;
constexpr unsigned kRgbaInteger = 0x8D99;

std::vector<FormatEntry> build() {
    std::vector<FormatEntry> t;
    const auto add = [&](DXGI_FORMAT d, const char* n, int vk, unsigned gi, unsigned gf, unsigned gt, int d9, bool packed = false) {
        t.push_back({d, n, vk, gi, gf, gt, d9, packed});
    };
    add(DXGI_FORMAT_R8_UNORM, "R8_UNORM", VK_FORMAT_R8_UNORM, 0x8229, kRed, kUByte, D3DFMT_L8);
    add(DXGI_FORMAT_R8_SNORM, "R8_SNORM", VK_FORMAT_R8_SNORM, 0x8F94, kRed, kByte, 0);
    add(DXGI_FORMAT_R8_UINT, "R8_UINT", VK_FORMAT_R8_UINT, 0x8232, kRedInteger, kUByte, 0);
    add(DXGI_FORMAT_R8_SINT, "R8_SINT", VK_FORMAT_R8_SINT, 0x8231, kRedInteger, kByte, 0);
    add(DXGI_FORMAT_R8G8_UNORM, "R8G8_UNORM", VK_FORMAT_R8G8_UNORM, 0x822B, kRg, kUByte, 0);
    add(DXGI_FORMAT_R8G8_SNORM, "R8G8_SNORM", VK_FORMAT_R8G8_SNORM, 0x8F95, kRg, kByte, D3DFMT_V8U8);
    add(DXGI_FORMAT_R8G8_UINT, "R8G8_UINT", VK_FORMAT_R8G8_UINT, 0x8238, kRgInteger, kUByte, 0);
    add(DXGI_FORMAT_R8G8_SINT, "R8G8_SINT", VK_FORMAT_R8G8_SINT, 0x8237, kRgInteger, kByte, 0);
    add(DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM, 0x8058, kRgba, kUByte, 0);
    add(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "R8G8B8A8_UNORM_SRGB", VK_FORMAT_R8G8B8A8_SRGB, 0x8C43, kRgba, kUByte, 0);
    add(DXGI_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8_SNORM", VK_FORMAT_R8G8B8A8_SNORM, 0x8F97, kRgba, kByte, D3DFMT_Q8W8V8U8);
    add(DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT", VK_FORMAT_R8G8B8A8_UINT, 0x8D7C, kRgbaInteger, kUByte, 0);
    add(DXGI_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT", VK_FORMAT_R8G8B8A8_SINT, 0x8D8E, kRgbaInteger, kByte, 0);
    add(DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM", VK_FORMAT_B8G8R8A8_UNORM, 0x8058, kBgra, kUByte, D3DFMT_A8R8G8B8);
    add(DXGI_FORMAT_R16_UNORM, "R16_UNORM", VK_FORMAT_R16_UNORM, 0x822A, kRed, kUShort, 0);
    add(DXGI_FORMAT_R16_SNORM, "R16_SNORM", VK_FORMAT_R16_SNORM, 0x8F98, kRed, kShort, 0);
    add(DXGI_FORMAT_R16_UINT, "R16_UINT", VK_FORMAT_R16_UINT, 0x8234, kRedInteger, kUShort, 0);
    add(DXGI_FORMAT_R16_SINT, "R16_SINT", VK_FORMAT_R16_SINT, 0x8233, kRedInteger, kShort, 0);
    add(DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", VK_FORMAT_R16_SFLOAT, 0x822D, kRed, kHalf, D3DFMT_R16F);
    add(DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", VK_FORMAT_R16G16_UNORM, 0x822C, kRg, kUShort, D3DFMT_G16R16);
    add(DXGI_FORMAT_R16G16_SNORM, "R16G16_SNORM", VK_FORMAT_R16G16_SNORM, 0x8F99, kRg, kShort, 0);
    add(DXGI_FORMAT_R16G16_UINT, "R16G16_UINT", VK_FORMAT_R16G16_UINT, 0x823A, kRgInteger, kUShort, 0);
    add(DXGI_FORMAT_R16G16_SINT, "R16G16_SINT", VK_FORMAT_R16G16_SINT, 0x8239, kRgInteger, kShort, 0);
    add(DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT", VK_FORMAT_R16G16_SFLOAT, 0x822F, kRg, kHalf, D3DFMT_G16R16F);
    add(DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM", VK_FORMAT_R16G16B16A16_UNORM, 0x805B, kRgba, kUShort, D3DFMT_A16B16G16R16);
    add(DXGI_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16_SNORM", VK_FORMAT_R16G16B16A16_SNORM, 0x8F9B, kRgba, kShort, 0);
    add(DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT", VK_FORMAT_R16G16B16A16_UINT, 0x8D76, kRgbaInteger, kUShort, 0);
    add(DXGI_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT", VK_FORMAT_R16G16B16A16_SINT, 0x8D88, kRgbaInteger, kShort, 0);
    add(DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", VK_FORMAT_R16G16B16A16_SFLOAT, 0x881A, kRgba, kHalf, D3DFMT_A16B16G16R16F);
    add(DXGI_FORMAT_R32_UINT, "R32_UINT", VK_FORMAT_R32_UINT, 0x8236, kRedInteger, kUInt, 0);
    add(DXGI_FORMAT_R32_SINT, "R32_SINT", VK_FORMAT_R32_SINT, 0x8235, kRedInteger, kInt, 0);
    add(DXGI_FORMAT_R32_FLOAT, "R32_FLOAT", VK_FORMAT_R32_SFLOAT, 0x822E, kRed, kFloat, D3DFMT_R32F);
    add(DXGI_FORMAT_R32G32_UINT, "R32G32_UINT", VK_FORMAT_R32G32_UINT, 0x823C, kRgInteger, kUInt, 0);
    add(DXGI_FORMAT_R32G32_SINT, "R32G32_SINT", VK_FORMAT_R32G32_SINT, 0x823B, kRgInteger, kInt, 0);
    add(DXGI_FORMAT_R32G32_FLOAT, "R32G32_FLOAT", VK_FORMAT_R32G32_SFLOAT, 0x8230, kRg, kFloat, D3DFMT_G32R32F);
    add(DXGI_FORMAT_R32G32B32_UINT, "R32G32B32_UINT", VK_FORMAT_R32G32B32_UINT, 0x8D71, kRgbInteger, kUInt, 0);
    add(DXGI_FORMAT_R32G32B32_SINT, "R32G32B32_SINT", VK_FORMAT_R32G32B32_SINT, 0x8D83, kRgbInteger, kInt, 0);
    add(DXGI_FORMAT_R32G32B32_FLOAT, "R32G32B32_FLOAT", VK_FORMAT_R32G32B32_SFLOAT, 0x8815, kRgb, kFloat, 0);
    add(DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT", VK_FORMAT_R32G32B32A32_UINT, 0x8D70, kRgbaInteger, kUInt, 0);
    add(DXGI_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT", VK_FORMAT_R32G32B32A32_SINT, 0x8D82, kRgbaInteger, kInt, 0);
    add(DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT", VK_FORMAT_R32G32B32A32_SFLOAT, 0x8814, kRgba, kFloat, D3DFMT_A32B32G32R32F);
    add(DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM", VK_FORMAT_A2B10G10R10_UNORM_PACK32, 0x8059, kRgba, 0x8368, D3DFMT_A2B10G10R10, true);
    add(DXGI_FORMAT_R10G10B10A2_UINT, "R10G10B10A2_UINT", VK_FORMAT_A2B10G10R10_UINT_PACK32, 0x906F, kRgbaInteger, 0x8368, 0, true);
    add(DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", VK_FORMAT_B10G11R11_UFLOAT_PACK32, 0x8C3A, kRgb, 0x8C3B, 0, true);
    add(DXGI_FORMAT_R9G9B9E5_SHAREDEXP, "R9G9B9E5_SHAREDEXP", VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 0x8C3D, kRgb, 0x8C3E, 0, true);
    add(DXGI_FORMAT_B5G6R5_UNORM, "B5G6R5_UNORM", VK_FORMAT_R5G6B5_UNORM_PACK16, 0x8D62, kRgb, 0x8363, D3DFMT_R5G6B5, true);
    add(DXGI_FORMAT_B5G5R5A1_UNORM, "B5G5R5A1_UNORM", VK_FORMAT_A1R5G5B5_UNORM_PACK16, 0x8057, kBgra, 0x8366, D3DFMT_A1R5G5B5, true);
    add(DXGI_FORMAT_B4G4R4A4_UNORM, "B4G4R4A4_UNORM", 1000340000, 0x8056, kBgra, 0x8365, D3DFMT_A4R4G4B4, true);
    add(DXGI_FORMAT_BC1_UNORM, "BC1_UNORM", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 0x83F1, 0, 0, D3DFMT_DXT1);
    add(DXGI_FORMAT_BC2_UNORM, "BC2_UNORM", VK_FORMAT_BC2_UNORM_BLOCK, 0x83F2, 0, 0, D3DFMT_DXT3);
    add(DXGI_FORMAT_BC3_UNORM, "BC3_UNORM", VK_FORMAT_BC3_UNORM_BLOCK, 0x83F3, 0, 0, D3DFMT_DXT5);
    add(DXGI_FORMAT_BC4_UNORM, "BC4_UNORM", VK_FORMAT_BC4_UNORM_BLOCK, 0x8DBB, 0, 0, 0);
    add(DXGI_FORMAT_BC5_UNORM, "BC5_UNORM", VK_FORMAT_BC5_UNORM_BLOCK, 0x8DBD, 0, 0, 0);
    add(DXGI_FORMAT_BC6H_UF16, "BC6H_UF16", VK_FORMAT_BC6H_UFLOAT_BLOCK, 0x8E8F, 0, 0, 0);
    add(DXGI_FORMAT_BC7_UNORM, "BC7_UNORM", VK_FORMAT_BC7_UNORM_BLOCK, 0x8E8C, 0, 0, 0);
    return t;
}

}  // namespace

const std::vector<FormatEntry>& FormatTable::all() {
    static const std::vector<FormatEntry> table = build();
    return table;
}

const FormatEntry* FormatTable::find(DXGI_FORMAT format) {
    for (const FormatEntry& entry : all()) {
        if (entry.dxgi == format) {
            return &entry;
        }
    }
    return nullptr;
}

const char* FormatTable::name(DXGI_FORMAT format) {
    const FormatEntry* entry = find(format);
    return entry != nullptr ? entry->name : "unknown";
}

std::vector<DXGI_FORMAT> FormatTable::uncompressed_color() {
    std::vector<DXGI_FORMAT> out;
    for (const FormatEntry& entry : all()) {
        if (!FormatCodec::is_block(entry.dxgi)) {
            out.push_back(entry.dxgi);
        }
    }
    return out;
}

}  // namespace lsw::suites
