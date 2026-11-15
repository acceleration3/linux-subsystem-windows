#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d11_device.hpp"
#include "support/duplication_frame.hpp"

#include <windows.h>

#include <dxgi1_5.h>

#include <string>

namespace lsw::suites {
enum class DuplicationOpen { Ready, Unavailable, Failed };

class DuplicationSession {
public:
    [[nodiscard]] DuplicationOpen open(const RECT& area, std::string& error);

    [[nodiscard]] HRESULT acquire(UINT timeout_ms, DuplicationFrame& out, std::string& error);

    void crop_to(const RECT& output_relative) {
        crop_ = output_relative;
        cropped_ = true;
    }

    [[nodiscard]] const RECT& output_rect() const { return output_; }

    [[nodiscard]] bool used_duplicate_output1() const { return output1_used_; }

    [[nodiscard]] static std::string zero_frame_finding(int frames, long long last_present_time);

private:
    D3d11Device device_;
    Com<IDXGIOutputDuplication> duplication_;
    Com<ID3D11Texture2D> staging_;
    RECT output_{};
    RECT crop_{};
    bool cropped_ = false;
    bool output1_used_ = false;
};
}  // namespace lsw::suites
