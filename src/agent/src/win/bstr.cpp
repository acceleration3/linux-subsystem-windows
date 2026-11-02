#include "lsw/agent/win/bstr.hpp"

#include "lsw/agent/util/text_encoding.hpp"

namespace lsw::agent::win {

Bstr Bstr::from_utf8(const std::string& utf8) {
    const std::wstring wide = util::TextEncoding::to_wide(utf8);
    Bstr out;
    *out.put() = ::SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
    return out;
}

std::string Bstr::to_utf8(BSTR borrowed) {
    if (borrowed == nullptr) {
        return {};
    }
    const auto len = ::SysStringLen(borrowed);
    if (len == 0) {
        return {};
    }
    return util::TextEncoding::to_utf8(std::wstring(borrowed, len));
}

}
