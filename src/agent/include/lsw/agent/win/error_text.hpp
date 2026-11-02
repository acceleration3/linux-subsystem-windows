#pragma once

#include <string>

namespace lsw::agent::win {
class ErrorText {
public:
    ErrorText() = delete;

    [[nodiscard]] static std::string hresult(long hr);

    [[nodiscard]] static std::string win32(unsigned long code);

    [[nodiscard]] static std::string last_error();
};

}
