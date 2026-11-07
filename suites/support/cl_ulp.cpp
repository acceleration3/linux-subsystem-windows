#include "support/cl_ulp.hpp"

#include <cmath>

namespace lsw::suites {

namespace {

double measure(long double exact, long double got, int mantissa_bits, int minimum_exponent) {
    if (std::isnan(got)) {
        return 1e30;
    }
    if (std::isinf(got) || std::isinf(exact)) {
        return got == exact ? 0.0 : 1e30;
    }
    int exponent = 0;
    std::frexp(std::fabs(exact), &exponent);
    int ulp_exponent = exponent - mantissa_bits;
    if (ulp_exponent < minimum_exponent) {
        ulp_exponent = minimum_exponent;
    }
    const long double ulp = std::ldexp(1.0L, ulp_exponent);
    return static_cast<double>(std::fabs(got - exact) / ulp);
}

}  // namespace

double ClUlp::error(long double exact, float got) {
    return measure(exact, static_cast<long double>(got), 24, -149);
}

double ClUlp::error(long double exact, double got) {
    return measure(exact, static_cast<long double>(got), 53, -1074);
}

}  // namespace lsw::suites
