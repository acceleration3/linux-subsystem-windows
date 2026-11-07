#pragma once

#include "support/frame_stats.hpp"
#include "support/present_result.hpp"
#include "support/test_window.hpp"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites {
class PresentPath {
public:
    virtual ~PresentPath() = default;

    [[nodiscard]] virtual std::string id() const = 0;

    [[nodiscard]] virtual std::string api() const = 0;

    [[nodiscard]] virtual bool open(int x, int y, int width, int height, std::string& error) = 0;

    [[nodiscard]] virtual PresentResult present(int frame, UINT interval) = 0;

    [[nodiscard]] virtual bool supports_interval(UINT interval) const = 0;

    [[nodiscard]] virtual bool supports_image() const { return false; }

    [[nodiscard]] virtual PresentResult present_image(const std::vector<std::uint32_t>& xrgb, UINT interval);

    [[nodiscard]] virtual bool drain(std::string& error) = 0;

    [[nodiscard]] virtual bool resize(int width, int height, std::string& error) = 0;

    virtual void close() = 0;

    [[nodiscard]] virtual HWND hwnd() const = 0;

    [[nodiscard]] virtual bool flip_model() const = 0;

    [[nodiscard]] virtual bool frame_stats(FrameStats& out);

    [[nodiscard]] virtual bool last_present_count(std::uint64_t& out);

    [[nodiscard]] int width() const { return width_; }

    [[nodiscard]] int height() const { return height_; }

    [[nodiscard]] RECT client() const;

    [[nodiscard]] static std::unique_ptr<PresentPath> make(const std::string& id);

    [[nodiscard]] static std::vector<std::string> ids();

    [[nodiscard]] static std::string api_of(const std::string& id);

    [[nodiscard]] static std::vector<std::string> ids_of(const std::string& api);

    [[nodiscard]] static std::unique_ptr<PresentPath> make_native(const std::string& id);

protected:
    int width_ = 0;
    int height_ = 0;
};
}  // namespace lsw::suites
