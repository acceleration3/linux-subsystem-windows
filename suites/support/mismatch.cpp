#include "support/mismatch.hpp"

#include "core/text.hpp"

namespace lsw::suites {

std::string Mismatch::describe() const {
    if (pixel) {
        return "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") expected 0x" +
               Text::hex(expected, 8) + ", actual 0x" + Text::hex(actual, 8);
    }
    return "byte offset " + std::to_string(offset) + " expected 0x" + Text::hex(expected, 2) +
           ", actual 0x" + Text::hex(actual, 2);
}

}  // namespace lsw::suites
