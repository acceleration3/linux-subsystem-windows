#include "core/json.hpp"

#include <cmath>
#include <cstdio>

namespace lsw::suites {

namespace {

void write_string(std::string& out, const std::string& text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void newline(std::string& out, int indent, int depth) {
    if (indent <= 0) {
        return;
    }
    out += '\n';
    out.append(static_cast<std::size_t>(indent * depth), ' ');
}

}  // namespace

Json Json::array() {
    Json value;
    value.kind_ = Kind::Array;
    return value;
}

Json Json::object() {
    Json value;
    value.kind_ = Kind::Object;
    return value;
}

Json& Json::push(Json value) {
    values_.push_back(std::move(value));
    return *this;
}

Json& Json::set(const std::string& key, Json value) {
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            values_[i] = std::move(value);
            return *this;
        }
    }
    keys_.push_back(key);
    values_.push_back(std::move(value));
    return *this;
}

double Json::as_real() const {
    return kind_ == Kind::Real ? real_ : static_cast<double>(int_);
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    return out;
}

void Json::write(std::string& out, int indent, int depth) const {
    switch (kind_) {
    case Kind::Null:
        out += "null";
        break;
    case Kind::Bool:
        out += int_ != 0 ? "true" : "false";
        break;
    case Kind::Int:
        out += std::to_string(int_);
        break;
    case Kind::Real:
        if (std::isfinite(real_)) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.15g", real_);
            out += buffer;
        } else {
            out += "null";
        }
        break;
    case Kind::String:
        write_string(out, text_);
        break;
    case Kind::Array:
    case Kind::Object: {
        const bool object = kind_ == Kind::Object;
        out += object ? '{' : '[';
        for (std::size_t i = 0; i < values_.size(); ++i) {
            out += i == 0 ? "" : ",";
            newline(out, indent, depth + 1);
            if (object) {
                write_string(out, keys_[i]);
                out += indent > 0 ? ": " : ":";
            }
            values_[i].write(out, indent, depth + 1);
        }
        if (!values_.empty()) {
            newline(out, indent, depth);
        }
        out += object ? '}' : ']';
        break;
    }
    }
}

}  // namespace lsw::suites
