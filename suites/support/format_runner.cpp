#include "support/format_runner.hpp"

#include "support/format_codec.hpp"

namespace lsw::suites {

void FormatRunner::layout(DXGI_FORMAT format, UINT width, UINT height, UINT& row_bytes, UINT& rows) {
    const UINT element = static_cast<UINT>(FormatCodec::element_bytes(format));
    if (FormatCodec::is_block(format)) {
        row_bytes = ((width + 3) / 4) * element;
        rows = (height + 3) / 4;
    } else {
        row_bytes = width * element;
        rows = height;
    }
}

std::string FormatRunner::sample_source(const std::string& type, UINT width, UINT height) {
    const std::string convert = type == "uint4" ? "v" : "asuint(v)";
    return "Texture2D<" + type + "> t : register(t0);\nRWByteAddressBuffer o : register(u0);\n[numthreads(8, 8, 1)]\n" +
           "void main(uint3 id : SV_DispatchThreadID) {\n  if (id.x < " + std::to_string(width) + " && id.y < " + std::to_string(height) + ") {\n    " + type +
           " v = t.Load(int3(id.xy, 0));\n    o.Store4((id.y * " + std::to_string(width) + " + id.x) * 16, " + convert + ");\n  }\n}\n";
}

std::string FormatRunner::pixel_source(const std::string& type, const std::string& expression, const std::string& prelude) {
    return prelude + type + " main(float4 pos : SV_Position) : SV_Target { return " + expression + "; }\n";
}

}  // namespace lsw::suites
