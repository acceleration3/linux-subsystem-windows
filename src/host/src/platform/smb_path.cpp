#include "lsw/host/platform/smb_path.hpp"

#include <cerrno>
#include <cstddef>

namespace lsw::host::platform {

namespace {

constexpr std::size_t kMaxPath = 4096;
constexpr std::size_t kMaxName = 255;

bool bad_char(char c) {
    switch (c) {
    case '\\':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
        return true;
    default:
        return static_cast<unsigned char>(c) < 0x20;
    }
}

int check_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return -EINVAL;
    }
    if (name.size() > kMaxName) {
        return -ENAMETOOLONG;
    }
    if (name.back() == '.' || name.back() == ' ') {
        return -EINVAL;
    }
    for (const char c : name) {
        if (bad_char(c)) {
            return -EINVAL;
        }
    }
    return 0;
}

}  // namespace

int SmbPath::check(const std::string& path) {
    if (path.empty() || path.front() != '/') {
        return -EINVAL;
    }
    if (path.size() > kMaxPath) {
        return -ENAMETOOLONG;
    }
    std::size_t start = 1;
    while (start < path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        if (const int r = check_name(path.substr(start, end - start)); r != 0) {
            return r;
        }
        start = end + 1;
    }
    return 0;
}

std::string SmbPath::url(const std::string& base, const std::string& path) {
    std::string out = base;
    out.reserve(base.size() + path.size() + 8);
    for (const char c : path) {
        if (c == '%') {
            out += "%25";
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace lsw::host::platform
