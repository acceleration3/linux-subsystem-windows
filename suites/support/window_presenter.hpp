#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace lsw::suites {
class WindowPresenter {
public:
    virtual ~WindowPresenter() = default;

    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual bool open(int x, int y, int width, int height, std::string& error) = 0;

    [[nodiscard]] virtual bool present(int frame, std::string& error) = 0;

    virtual void close() = 0;

    [[nodiscard]] virtual HWND hwnd() const = 0;

    [[nodiscard]] virtual bool window_dc_has_content() const { return false; }

    [[nodiscard]] virtual bool flip_model() const { return false; }

    [[nodiscard]] static std::unique_ptr<WindowPresenter> make(const std::string& api, bool flip = false);

    [[nodiscard]] static std::vector<std::string> apis();
};
}  // namespace lsw::suites
