#pragma once

#include "support/d3d11_device.hpp"
#include "support/wgc_frame.hpp"

#include <windows.h>

#include <memory>
#include <string>

namespace lsw::suites {
enum class WgcOpen { Ready, Unavailable, Failed };

class WgcSession {
public:
    WgcSession();
    WgcSession(const WgcSession&) = delete;
    WgcSession& operator=(const WgcSession&) = delete;
    ~WgcSession();

    [[nodiscard]] WgcOpen open(HWND window, D3d11Device& device, std::string& error);

    [[nodiscard]] bool next(WgcFrame& out, int timeout_ms, std::string& error);

    [[nodiscard]] bool latest(WgcFrame& out, int timeout_ms, std::string& error);

    [[nodiscard]] bool recreate(int width, int height, std::string& error);

    [[nodiscard]] bool wait_closed(int timeout_ms);

    [[nodiscard]] bool close(std::string& error);

    [[nodiscard]] static POINT client_offset(HWND window);

    [[nodiscard]] static RECT frame_bounds(HWND window);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lsw::suites
