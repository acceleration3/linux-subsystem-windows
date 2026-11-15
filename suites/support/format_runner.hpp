#pragma once

#include <dxgiformat.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class FormatRunner {
public:
    using Bytes = std::vector<std::uint8_t>;

    virtual ~FormatRunner() = default;

    [[nodiscard]] virtual const char* label() const = 0;

    [[nodiscard]] virtual bool support(DXGI_FORMAT format, UINT& support1) = 0;

    [[nodiscard]] virtual bool roundtrip(DXGI_FORMAT format, UINT width, UINT height, const Bytes& in, Bytes& out,
                                         std::string& error) = 0;

    [[nodiscard]] virtual bool clear(DXGI_FORMAT format, UINT width, UINT height, const float color[4], Bytes& out,
                                     std::string& error) = 0;

    [[nodiscard]] virtual bool draw(DXGI_FORMAT format, UINT width, UINT height, const std::string& type,
                                    const std::string& expression, const float* clear_color, bool alpha_blend, Bytes& out,
                                    std::string& error, const std::string& prelude = std::string()) = 0;

    [[nodiscard]] virtual bool sample(DXGI_FORMAT texture_format, DXGI_FORMAT view_format, const std::string& type, UINT width,
                                      UINT height, const Bytes& in, std::vector<std::uint32_t>& words,
                                      std::string& error) = 0;

    static void layout(DXGI_FORMAT format, UINT width, UINT height, UINT& row_bytes, UINT& rows);

    [[nodiscard]] static std::string sample_source(const std::string& type, UINT width, UINT height);

    [[nodiscard]] static std::string pixel_source(const std::string& type, const std::string& expression,
                                                  const std::string& prelude = std::string());
};
}  // namespace lsw::suites
