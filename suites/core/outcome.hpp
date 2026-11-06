#pragma once

#include <string>
#include <utility>

namespace lsw::suites {
enum class Status { Ok, Fail, Skip, Timeout, Crash };

class Outcome {
public:
    [[nodiscard]] static Outcome ok() { return {Status::Ok, {}}; }

    [[nodiscard]] static Outcome fail(std::string reason) {
        return {Status::Fail, std::move(reason)};
    }

    [[nodiscard]] static Outcome skip(std::string reason) {
        return {Status::Skip, std::move(reason)};
    }

    [[nodiscard]] Status status() const { return status_; }

    [[nodiscard]] const std::string& reason() const { return reason_; }

    [[nodiscard]] static const char* name(Status status);

private:
    Outcome(Status status, std::string reason) : status_(status), reason_(std::move(reason)) {}

    Status status_;
    std::string reason_;
};
}  // namespace lsw::suites
