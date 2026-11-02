#pragma once

#include <string>

namespace lsw::agent::install {
class UrlFileName {
public:
    UrlFileName() = delete;

    [[nodiscard]] static std::wstring of(const std::wstring& url);
};

}
