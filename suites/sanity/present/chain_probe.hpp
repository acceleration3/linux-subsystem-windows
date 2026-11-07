#pragma once

#include "support/bitmap.hpp"

#include <windows.h>

#include <string>

namespace lsw::suites::sanity::present {
class ChainProbe {
public:
    [[nodiscard]] static bool within(std::uint32_t a, std::uint32_t b, int tolerance);

    [[nodiscard]] static bool quadrants(const Bitmap& image, const RECT& content, int frame, int tolerance, std::string& detail);

    [[nodiscard]] static bool poll_quadrants(const RECT& client, const RECT& content_in_client, int frame, int tolerance, int timeout_ms, std::string& detail);

    [[nodiscard]] static float linear_of(int value8);
};
}  // namespace lsw::suites::sanity::present
