#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::win {
class Environment {
public:
    Environment() = delete;

    using Override = std::pair<std::string, std::string>;

    [[nodiscard]] static std::wstring expand(const std::wstring& s);

    [[nodiscard]] static std::vector<wchar_t> block_with(const std::vector<Override>& overrides);

    [[nodiscard]] static std::vector<wchar_t> block_from(const wchar_t* base,
                                                         const std::vector<Override>& overrides);
};

}
