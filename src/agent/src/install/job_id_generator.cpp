#include "lsw/agent/install/job_id_generator.hpp"

namespace lsw::agent::install {

std::string JobIdGenerator::next() {
    const std::lock_guard<std::mutex> lock(mu_);
    return "job-" + std::to_string(next_id_++);
}

}  // namespace lsw::agent::install
