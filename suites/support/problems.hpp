#pragma once

#include "core/outcome.hpp"

#include <string>

namespace lsw::suites {
class Problems {
public:
    void add(const std::string& text) {
        if (!text_.empty()) {
            text_ += "; ";
        }
        text_ += text;
        ++count_;
    }

    [[nodiscard]] bool empty() const { return count_ == 0; }

    [[nodiscard]] Outcome outcome() const { return empty() ? Outcome::ok() : Outcome::fail(text_); }

private:
    std::string text_;
    int count_ = 0;
};
}  // namespace lsw::suites
