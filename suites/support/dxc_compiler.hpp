#pragma once

#include <windows.h>
#include <dxcapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class DxcCompiler {
public:
    DxcCompiler() = default;
    DxcCompiler(const DxcCompiler&) = delete;
    DxcCompiler& operator=(const DxcCompiler&) = delete;
    ~DxcCompiler();

    [[nodiscard]] bool open(std::string& error);

    [[nodiscard]] bool compile(const std::string& source, const wchar_t* entry, const wchar_t* target, std::vector<std::uint8_t>& code,
                               std::string& log);

private:
    HMODULE module_ = nullptr;
    IDxcCompiler* compiler_ = nullptr;
    IDxcLibrary* library_ = nullptr;
};
}  // namespace lsw::suites
