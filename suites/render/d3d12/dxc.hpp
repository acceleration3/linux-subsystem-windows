#pragma once

#include <windows.h>
#include <dxcapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites::render::d3d12 {
class Dxc {
public:
    Dxc() = default;
    Dxc(const Dxc&) = delete;
    Dxc& operator=(const Dxc&) = delete;
    ~Dxc();

    [[nodiscard]] bool open(std::string& error);

    [[nodiscard]] bool compile(const std::string& source, const wchar_t* entry, const wchar_t* target, const std::vector<const wchar_t*>& arguments,
                               std::vector<std::uint8_t>& code, std::string& log);

private:
    HMODULE module_ = nullptr;
    IDxcCompiler* compiler_ = nullptr;
    IDxcLibrary* library_ = nullptr;
};
}  // namespace lsw::suites::render::d3d12
