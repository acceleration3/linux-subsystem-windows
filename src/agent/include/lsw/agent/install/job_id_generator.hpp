#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace lsw::agent::install {

// Hands out monotonically increasing job ids. Thread-safe; jobs are
// started from whichever thread served the RPC.
class JobIdGenerator {
public:
    [[nodiscard]] std::string next();

private:
    std::mutex mu_;
    std::uint64_t next_id_ = 1;
};

}  // namespace lsw::agent::install
