#pragma once

#include "support/format_codec.hpp"
#include "support/format_table.hpp"
#include "support/gl_api.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites::sanity::fmt {
class GlFormatIo {
public:
    using Bytes = std::vector<std::uint8_t>;

    enum class Result { Done, Unsupported, Failed };

    [[nodiscard]] static bool reported_supported(const GlApi& api, unsigned internal);

    [[nodiscard]] static Result upload_read(const GlApi& api, const FormatEntry& entry, int width, int height, const Bytes& in, Bytes& out,
                                            std::string& error);

    [[nodiscard]] static Result clear_read(const GlApi& api, const FormatEntry& entry, const FormatSpec& spec, const double values[4],
                                           int width, int height, Bytes& out, std::string& error);

    [[nodiscard]] static std::size_t texel_bytes(const FormatEntry& entry) {
        return static_cast<std::size_t>(FormatCodec::element_bytes(entry.dxgi));
    }
};
}  // namespace lsw::suites::sanity::fmt
