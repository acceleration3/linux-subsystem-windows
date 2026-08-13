#include "lsw/agent/install/url_file_name.hpp"

namespace lsw::agent::install {

std::wstring UrlFileName::of(const std::wstring& url) {
    const auto query = url.find_first_of(L"?#");
    const std::wstring path = (query == std::wstring::npos) ? url : url.substr(0, query);
    const auto slash = path.find_last_of(L'/');
    std::wstring base = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    return base.empty() ? std::wstring{L"download.bin"} : base;
}

}  // namespace lsw::agent::install
