#include "core/child_protocol.hpp"

#include "core/text.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace lsw::suites {

namespace {

constexpr const char* kPrefix = "@@lsw ";

Status parse_status(const std::string& name) {
    if (name == "ok") {
        return Status::Ok;
    }
    if (name == "skip") {
        return Status::Skip;
    }
    return Status::Fail;
}

void decode_measure(const std::string& rest, Json& measures) {
    if (rest.size() < 3) {
        return;
    }
    const char type = rest[0];
    const std::size_t space = rest.find(' ', 2);
    if (space == std::string::npos) {
        return;
    }
    const std::string key = rest.substr(2, space - 2);
    const std::string value = Text::unescape(rest.substr(space + 1));
    if (type == 's') {
        measures.set(key, value);
    } else if (type == 'b') {
        measures.set(key, value == "1");
    } else if (value.find_first_of(".eEn") != std::string::npos) {
        measures.set(key, std::strtod(value.c_str(), nullptr));
    } else {
        measures.set(key, static_cast<long long>(std::strtoll(value.c_str(), nullptr, 10)));
    }
}

}  // namespace

std::string ChildProtocol::encode(const Context& context, const Outcome& outcome) {
    std::string out;
    const Json& measures = context.measures();
    for (std::size_t i = 0; i < measures.size(); ++i) {
        const Json& value = measures.values()[i];
        char type = 'n';
        std::string text;
        switch (value.kind()) {
        case Json::Kind::String:
            type = 's';
            text = value.as_string();
            break;
        case Json::Kind::Bool:
            type = 'b';
            text = value.as_bool() ? "1" : "0";
            break;
        case Json::Kind::Int:
            text = std::to_string(value.as_int());
            break;
        case Json::Kind::Real:
            text = value.dump(0);
            if (text.find_first_of(".eEn") == std::string::npos) {
                text += ".0";
            }
            break;
        default:
            continue;
        }
        out += std::string(kPrefix) + "measure " + type + " " + measures.keys()[i] + " " +
               Text::escape(text) + "\n";
    }
    for (const std::string& path : context.artifacts()) {
        out += std::string(kPrefix) + "artifact " + Text::escape(path) + "\n";
    }
    for (const std::string& note : context.notes()) {
        out += std::string(kPrefix) + "note " + Text::escape(note) + "\n";
    }
    out += std::string(kPrefix) + "reason " + Text::escape(outcome.reason()) + "\n";
    out += std::string(kPrefix) + "status " + Outcome::name(outcome.status()) + "\n";
    return out;
}

void ChildProtocol::phase(const std::string& text) {
    const std::string line = std::string(kPrefix) + "phase " + Text::escape(text) + "\n";
    std::fputs(line.c_str(), stdout);
    std::fflush(stdout);
}

bool ChildProtocol::decode(const std::string& output, CheckResult& result) {
    bool have_status = false;
    std::string plain;
    std::istringstream lines(output);
    std::string line;
    const std::string prefix = kPrefix;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, prefix.size(), prefix) != 0) {
            plain += line + "\n";
            continue;
        }
        const std::string body = line.substr(prefix.size());
        const std::size_t space = body.find(' ');
        const std::string verb = body.substr(0, space);
        const std::string rest = space == std::string::npos ? "" : body.substr(space + 1);
        if (verb == "status") {
            result.status = parse_status(rest);
            have_status = true;
        } else if (verb == "reason") {
            result.reason = Text::unescape(rest);
        } else if (verb == "artifact") {
            result.artifacts.push_back(Text::unescape(rest));
        } else if (verb == "phase") {
            result.phase = Text::unescape(rest);
        } else if (verb == "note") {
            result.notes.push_back(Text::unescape(rest));
        } else if (verb == "measure") {
            decode_measure(rest, result.measures);
        }
    }
    result.output = std::move(plain);
    return have_status;
}

}  // namespace lsw::suites
