#pragma once

namespace lsw::suites {
class ClUlp {
public:
    [[nodiscard]] static double error(long double exact, float got);

    [[nodiscard]] static double error(long double exact, double got);
};
}  // namespace lsw::suites
