#pragma once

#include <string>

namespace lsw::suites {
class ExeIdentity {
public:
    static constexpr int kBitness = static_cast<int>(sizeof(void*) * 8);

    [[nodiscard]] static std::string name() { return "lsw-suites" + std::to_string(kBitness); }
};
}  // namespace lsw::suites
