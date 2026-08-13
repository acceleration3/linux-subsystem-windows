#include "lsw/agent/uia/bstr_text.hpp"

#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>
#include <oleauto.h>

namespace lsw::agent::uia {

std::string BstrText::to_utf8(void* bstr) {
    auto* b = static_cast<BSTR>(bstr);
    if (b == nullptr) {
        return {};
    }
    const auto len = ::SysStringLen(b);
    if (len == 0) {
        return {};
    }
    // Length-delimited on purpose: a BSTR may contain embedded NULs, and
    // treating it as a C string would truncate there.
    return util::TextEncoding::to_utf8(std::wstring(b, len));
}

}  // namespace lsw::agent::uia
