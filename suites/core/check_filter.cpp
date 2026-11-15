#include "core/check_filter.hpp"

#include "core/glob.hpp"

#include <algorithm>

namespace lsw::suites {

std::vector<std::string> CheckFilter::split(const std::string& list) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t comma = list.find(',', start);
        if (comma == std::string::npos) {
            comma = list.size();
        }
        if (comma > start) {
            parts.push_back(list.substr(start, comma - start));
        }
        start = comma + 1;
    }
    return parts;
}

CheckFilter::CheckFilter(const std::string& globs, const std::string& tags)
    : tags_(split(tags)) {
    for (const std::string& part : split(globs)) {
        if (part[0] == '-') {
            exclude_.push_back(part.substr(1));
        } else {
            include_.push_back(part);
        }
    }
}

bool CheckFilter::accepts(const std::string& id, const std::vector<std::string>& tags) const {
    if (!include_.empty() &&
        std::none_of(include_.begin(), include_.end(),
                     [&](const std::string& glob) { return Glob::match(glob, id); })) {
        return false;
    }
    if (std::any_of(exclude_.begin(), exclude_.end(),
                    [&](const std::string& glob) { return Glob::match(glob, id); })) {
        return false;
    }
    if (tags_.empty()) {
        return true;
    }
    return std::any_of(tags_.begin(), tags_.end(), [&](const std::string& wanted) {
        return std::find(tags.begin(), tags.end(), wanted) != tags.end();
    });
}

}  // namespace lsw::suites
