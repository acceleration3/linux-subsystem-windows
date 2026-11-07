#pragma once

#include "support/d3d12_rig.hpp"
#include "support/format_runner.hpp"

namespace lsw::suites {
class D3d12FormatRunner : public FormatRunner {
public:
    explicit D3d12FormatRunner(D3d12Rig& rig) : rig_(rig) {}

    [[nodiscard]] const char* label() const override { return "D3D12"; }

    [[nodiscard]] bool support(DXGI_FORMAT format, UINT& support1) override;

    [[nodiscard]] bool roundtrip(DXGI_FORMAT format, UINT width, UINT height, const Bytes& in, Bytes& out,
                                 std::string& error) override;

    [[nodiscard]] bool clear(DXGI_FORMAT format, UINT width, UINT height, const float color[4], Bytes& out,
                             std::string& error) override;

    [[nodiscard]] bool draw(DXGI_FORMAT format, UINT width, UINT height, const std::string& type, const std::string& expression,
                            const float* clear_color, bool alpha_blend, Bytes& out, std::string& error,
                            const std::string& prelude = std::string()) override;

    [[nodiscard]] bool sample(DXGI_FORMAT texture_format, DXGI_FORMAT view_format, const std::string& type, UINT width,
                              UINT height, const Bytes& in, std::vector<std::uint32_t>& words, std::string& error) override;

private:
    D3d12Rig& rig_;
};
}  // namespace lsw::suites
