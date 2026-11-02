#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace lsw::agent::install {
class JobIdGenerator {
public:
    [[nodiscard]] std::string next();

private:
    std::mutex mu_;
    std::uint64_t next_id_ = 1;
};

}
