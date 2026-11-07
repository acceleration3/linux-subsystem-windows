#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class Json {
public:
    enum class Kind { Null, Bool, Int, Real, String, Array, Object };

    Json() = default;
    Json(bool value) : kind_(Kind::Bool), int_(value ? 1 : 0) {}
    Json(int value) : kind_(Kind::Int), int_(value) {}
    Json(unsigned value) : kind_(Kind::Int), int_(value) {}
    Json(long value) : kind_(Kind::Int), int_(value) {}
    Json(unsigned long value) : kind_(Kind::Int), int_(static_cast<std::int64_t>(value)) {}
    Json(long long value) : kind_(Kind::Int), int_(value) {}
    Json(unsigned long long value) : kind_(Kind::Int), int_(static_cast<std::int64_t>(value)) {}
    Json(double value) : kind_(Kind::Real), real_(value) {}
    Json(const char* value) : kind_(Kind::String), text_(value) {}
    Json(std::string value) : kind_(Kind::String), text_(std::move(value)) {}

    [[nodiscard]] static Json array();

    [[nodiscard]] static Json object();

    Json& push(Json value);

    Json& set(const std::string& key, Json value);

    [[nodiscard]] const Json* find(const std::string& key) const;

    [[nodiscard]] Kind kind() const { return kind_; }

    [[nodiscard]] bool as_bool() const { return int_ != 0; }

    [[nodiscard]] std::int64_t as_int() const { return int_; }

    [[nodiscard]] double as_real() const;

    [[nodiscard]] const std::string& as_string() const { return text_; }

    [[nodiscard]] std::size_t size() const { return values_.size(); }

    [[nodiscard]] const std::vector<std::string>& keys() const { return keys_; }

    [[nodiscard]] const std::vector<Json>& values() const { return values_; }

    [[nodiscard]] std::string dump(int indent = 2) const;

private:
    void write(std::string& out, int indent, int depth) const;

    Kind kind_ = Kind::Null;
    std::int64_t int_ = 0;
    double real_ = 0.0;
    std::string text_;
    std::vector<std::string> keys_;
    std::vector<Json> values_;
};
}  // namespace lsw::suites
