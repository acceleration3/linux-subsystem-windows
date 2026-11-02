#include "lsw/base/log_format.hpp"

#include <cctype>
#include <ctime>

namespace lsw::base {

namespace {

void put_digits(std::string& out, long value, int width) {
    char buf[8];
    for (int i = width - 1; i >= 0; --i) {
        buf[i] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    out.append(buf, static_cast<std::size_t>(width));
}

bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != b[i]) {
            return false;
        }
    }
    return true;
}

}

const char* LogFormat::level_name(int level) noexcept {
    switch (level) {
    case 0: return "trace";
    case 1: return "debug";
    case 2: return "info";
    case 3: return "warn";
    default: return "error";
    }
}

int LogFormat::level_from_name(std::string_view name, int fallback) noexcept {
    if (equals_ignore_case(name, "trace")) return 0;
    if (equals_ignore_case(name, "debug")) return 1;
    if (equals_ignore_case(name, "info")) return 2;
    if (equals_ignore_case(name, "warn") || equals_ignore_case(name, "warning")) return 3;
    if (equals_ignore_case(name, "error") || equals_ignore_case(name, "err")) return 4;
    return fallback;
}

void LogFormat::append_line(std::string& out, std::int64_t ts_ms, int level,
                            std::string_view source, std::string_view text) {
    const std::time_t seconds = static_cast<std::time_t>(ts_ms / 1000);
    std::tm local{};
    ::localtime_r(&seconds, &local);
    put_digits(out, local.tm_year + 1900, 4);
    out += '-';
    put_digits(out, local.tm_mon + 1, 2);
    out += '-';
    put_digits(out, local.tm_mday, 2);
    out += ' ';
    put_digits(out, local.tm_hour, 2);
    out += ':';
    put_digits(out, local.tm_min, 2);
    out += ':';
    put_digits(out, local.tm_sec, 2);
    out += '.';
    put_digits(out, static_cast<long>(ts_ms % 1000), 3);
    out += ' ';
    const char* name = level_name(level);
    out += name;
    for (std::size_t n = std::char_traits<char>::length(name); n < 5; ++n) {
        out += ' ';
    }
    out += ' ';
    out.append(source);
    out += ' ';
    out.append(text);
    out += '\n';
}

}
