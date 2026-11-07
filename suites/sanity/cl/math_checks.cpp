#include "sanity/cl/math_checks.hpp"

#include "sanity/cl/kit.hpp"
#include "support/cl_ulp.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::cl {

namespace {

using Real = long double;

struct FloatFunction {
    const char* name;
    const char* expression;
    double ulp;
    double low;
    double high;
    double low_b;
    double high_b;
    double guard;
    std::function<Real(Real, Real, Real)> reference;
};

const std::vector<FloatFunction>& float_functions() {
    static const std::vector<FloatFunction> table = {
        {"add", "a + b", 0.0, -1000, 1000, -1000, 1000, 0, [](Real a, Real b, Real) { return a + b; }},
        {"sub", "a - b", 0.0, -1000, 1000, -1000, 1000, 0, [](Real a, Real b, Real) { return a - b; }},
        {"mul", "a * b", 0.0, -1000, 1000, -1000, 1000, 0, [](Real a, Real b, Real) { return a * b; }},
        {"div", "a / b", 2.5, -1000, 1000, 0.01, 1000, 0, [](Real a, Real b, Real) { return a / b; }},
        {"fma", "fma(a, b, c)", 0.0, -100, 100, -100, 100, 0, [](Real a, Real b, Real c) { return a * b + c; }},
        {"acos", "acos(a)", 4, -1, 1, 0, 0, 0, [](Real a, Real, Real) { return std::acos(a); }},
        {"acosh", "acosh(a)", 4, 1.001, 100, 0, 0, 0, [](Real a, Real, Real) { return std::acosh(a); }},
        {"asin", "asin(a)", 4, -1, 1, 0, 0, 0, [](Real a, Real, Real) { return std::asin(a); }},
        {"asinh", "asinh(a)", 4, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::asinh(a); }},
        {"atan", "atan(a)", 5, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::atan(a); }},
        {"atanh", "atanh(a)", 5, -0.99, 0.99, 0, 0, 0, [](Real a, Real, Real) { return std::atanh(a); }},
        {"atan2", "atan2(a, b)", 6, -10, 10, 0.1, 10, 0, [](Real a, Real b, Real) { return std::atan2(a, b); }},
        {"cbrt", "cbrt(a)", 2, -1000, 1000, 0, 0, 0, [](Real a, Real, Real) { return std::cbrt(a); }},
        {"ceil", "ceil(a)", 0.0, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::ceil(a); }},
        {"copysign", "copysign(a, b)", 0.0, -100, 100, -100, 100, 0, [](Real a, Real b, Real) { return std::copysign(a, b); }},
        {"cos", "cos(a)", 4, -8, 8, 0, 0, 1e-3, [](Real a, Real, Real) { return std::cos(a); }},
        {"cosh", "cosh(a)", 4, -8, 8, 0, 0, 0, [](Real a, Real, Real) { return std::cosh(a); }},
        {"erf", "erf(a)", 16, -3, 3, 0, 0, 1e-3, [](Real a, Real, Real) { return std::erf(a); }},
        {"erfc", "erfc(a)", 16, -3, 3, 0, 0, 1e-3, [](Real a, Real, Real) { return std::erfc(a); }},
        {"exp", "exp(a)", 3, -20, 20, 0, 0, 0, [](Real a, Real, Real) { return std::exp(a); }},
        {"exp2", "exp2(a)", 3, -30, 30, 0, 0, 0, [](Real a, Real, Real) { return std::exp2(a); }},
        {"exp10", "exp10(a)", 3, -10, 10, 0, 0, 0, [](Real a, Real, Real) { return std::pow(Real(10), a); }},
        {"expm1", "expm1(a)", 3, -5, 5, 0, 0, 0, [](Real a, Real, Real) { return std::expm1(a); }},
        {"fabs", "fabs(a)", 0.0, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::fabs(a); }},
        {"fdim", "fdim(a, b)", 0.0, -100, 100, -100, 100, 0, [](Real a, Real b, Real) { return a > b ? a - b : Real(0); }},
        {"floor", "floor(a)", 0.0, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::floor(a); }},
        {"fmax", "fmax(a, b)", 0.0, -100, 100, -100, 100, 0, [](Real a, Real b, Real) { return std::fmax(a, b); }},
        {"fmin", "fmin(a, b)", 0.0, -100, 100, -100, 100, 0, [](Real a, Real b, Real) { return std::fmin(a, b); }},
        {"fmod", "fmod(a, b)", 0.0, -100, 100, 0.1, 10, 0, [](Real a, Real b, Real) { return std::fmod(a, b); }},
        {"hypot", "hypot(a, b)", 4, -100, 100, -100, 100, 0, [](Real a, Real b, Real) { return std::hypot(a, b); }},
        {"log", "log(a)", 3, 0.001, 1000000, 0, 0, 1e-3, [](Real a, Real, Real) { return std::log(a); }},
        {"log2", "log2(a)", 3, 0.001, 1000000, 0, 0, 1e-3, [](Real a, Real, Real) { return std::log2(a); }},
        {"log10", "log10(a)", 3, 0.001, 1000000, 0, 0, 1e-3, [](Real a, Real, Real) { return std::log10(a); }},
        {"log1p", "log1p(a)", 2, -0.9, 10000, 0, 0, 0, [](Real a, Real, Real) { return std::log1p(a); }},
        {"nextafter", "nextafter(a, b)", 0.0, -100, 100, -100, 100, 0, [](Real a, Real b, Real) { return static_cast<Real>(std::nextafter(static_cast<float>(a), static_cast<float>(b))); }},
        {"pow", "pow(a, b)", 16, 0.1, 20, -4, 4, 0, [](Real a, Real b, Real) { return std::pow(a, b); }},
        {"remainder", "remainder(a, b)", 0.0, -100, 100, 0.1, 10, 0, [](Real a, Real b, Real) { return std::remainder(a, b); }},
        {"rint", "rint(a)", 0.0, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::rint(a); }},
        {"round", "round(a)", 0.0, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::round(a); }},
        {"rsqrt", "rsqrt(a)", 2, 0.001, 1000000, 0, 0, 0, [](Real a, Real, Real) { return Real(1) / std::sqrt(a); }},
        {"sin", "sin(a)", 4, -8, 8, 0, 0, 1e-3, [](Real a, Real, Real) { return std::sin(a); }},
        {"sinh", "sinh(a)", 4, -8, 8, 0, 0, 0, [](Real a, Real, Real) { return std::sinh(a); }},
        {"sqrt", "sqrt(a)", 3, 0, 1000000, 0, 0, 0, [](Real a, Real, Real) { return std::sqrt(a); }},
        {"tan", "tan(a)", 5, -1.4, 1.4, 0, 0, 1e-3, [](Real a, Real, Real) { return std::tan(a); }},
        {"tanh", "tanh(a)", 5, -8, 8, 0, 0, 0, [](Real a, Real, Real) { return std::tanh(a); }},
        {"tgamma", "tgamma(a)", 16, 0.2, 20, 0, 0, 0, [](Real a, Real, Real) { return std::tgamma(a); }},
        {"trunc", "trunc(a)", 0.0, -100, 100, 0, 0, 0, [](Real a, Real, Real) { return std::trunc(a); }},
    };
    return table;
}

struct IntOp {
    const char* name;
    const char* expression;
    std::function<std::uint32_t(std::uint32_t, std::uint32_t, std::uint32_t)> reference;
};

std::int32_t clamp64(std::int64_t v) {
    return static_cast<std::int32_t>(std::min<std::int64_t>(std::max<std::int64_t>(v, INT_MIN), INT_MAX));
}

std::uint32_t count_bits(std::uint32_t v) {
    std::uint32_t n = 0;
    for (; v != 0; v >>= 1) {
        n += v & 1;
    }
    return n;
}

std::uint32_t leading_zeros(std::uint32_t v) {
    std::uint32_t n = 0;
    for (std::uint32_t bit = 0x80000000u; bit != 0 && (v & bit) == 0; bit >>= 1) {
        ++n;
    }
    return n;
}

const std::vector<IntOp>& int_ops() {
    using U = std::uint32_t;
    static const std::vector<IntOp> table = {
        {"u add_sat", "add_sat(a, b)", [](U a, U b, U) { return static_cast<U>(std::min<std::uint64_t>(std::uint64_t(a) + b, UINT_MAX)); }},
        {"u sub_sat", "sub_sat(a, b)", [](U a, U b, U) { return a > b ? a - b : 0u; }},
        {"u abs_diff", "abs_diff(a, b)", [](U a, U b, U) { return a > b ? a - b : b - a; }},
        {"u clz", "clz(a)", [](U a, U, U) { return leading_zeros(a); }},
        {"u popcount", "popcount(a)", [](U a, U, U) { return count_bits(a); }},
        {"u mul_hi", "mul_hi(a, b)", [](U a, U b, U) { return static_cast<U>((std::uint64_t(a) * b) >> 32); }},
        {"u mad_hi", "mad_hi(a, b, c)", [](U a, U b, U c) { return static_cast<U>(((std::uint64_t(a) * b) >> 32) + c); }},
        {"u mad_sat", "mad_sat(a, b, c)", [](U a, U b, U c) { return static_cast<U>(std::min<std::uint64_t>(std::uint64_t(a) * b + c, UINT_MAX)); }},
        {"u rotate", "rotate(a, b)", [](U a, U b, U) { const U s = b & 31; return s == 0 ? a : (a << s) | (a >> (32 - s)); }},
        {"u hadd", "hadd(a, b)", [](U a, U b, U) { return static_cast<U>((std::uint64_t(a) + b) >> 1); }},
        {"u rhadd", "rhadd(a, b)", [](U a, U b, U) { return static_cast<U>((std::uint64_t(a) + b + 1) >> 1); }},
        {"u clamp", "clamp(a, min(b, c), max(b, c))", [](U a, U b, U c) { return std::min(std::max(a, std::min(b, c)), std::max(b, c)); }},
        {"u min", "min(a, b)", [](U a, U b, U) { return std::min(a, b); }},
        {"u max", "max(a, b)", [](U a, U b, U) { return std::max(a, b); }},
        {"u mul24", "mul24(a & 0xFFFFFFu, b & 0xFFFFFFu)", [](U a, U b, U) { return (a & 0xFFFFFFu) * (b & 0xFFFFFFu); }},
        {"u mad24", "mad24(a & 0xFFFFFFu, b & 0xFFFFFFu, c)", [](U a, U b, U c) { return (a & 0xFFFFFFu) * (b & 0xFFFFFFu) + c; }},
        {"u bitselect", "bitselect(a, b, c)", [](U a, U b, U c) { return (a & ~c) | (b & c); }},
        {"u shl", "a << (b & 31u)", [](U a, U b, U) { return a << (b & 31); }},
        {"u shr", "a >> (b & 31u)", [](U a, U b, U) { return a >> (b & 31); }},
        {"u div", "a / (b | 1u)", [](U a, U b, U) { return a / (b | 1u); }},
        {"u mod", "a % (b | 1u)", [](U a, U b, U) { return a % (b | 1u); }},
        {"u upsample", "upsample((ushort)a, (ushort)b)", [](U a, U b, U) { return (U(a & 0xFFFF) << 16) | (b & 0xFFFF); }},
        {"s add_sat", "as_uint(add_sat(sa, sb))", [](U a, U b, U) { return static_cast<U>(clamp64(std::int64_t(std::int32_t(a)) + std::int32_t(b))); }},
        {"s sub_sat", "as_uint(sub_sat(sa, sb))", [](U a, U b, U) { return static_cast<U>(clamp64(std::int64_t(std::int32_t(a)) - std::int32_t(b))); }},
        {"s abs", "abs(sa)", [](U a, U, U) { const std::int64_t v = std::int32_t(a); return static_cast<U>(v < 0 ? -v : v); }},
        {"s abs_diff", "abs_diff(sa, sb)", [](U a, U b, U) { const std::int64_t d = std::int64_t(std::int32_t(a)) - std::int32_t(b); return static_cast<U>(d < 0 ? -d : d); }},
        {"s clz", "as_uint(clz(sa))", [](U a, U, U) { return leading_zeros(a); }},
        {"s mul_hi", "as_uint(mul_hi(sa, sb))", [](U a, U b, U) { return static_cast<U>((std::int64_t(std::int32_t(a)) * std::int32_t(b)) >> 32); }},
        {"s hadd", "as_uint(hadd(sa, sb))", [](U a, U b, U) { return static_cast<U>((std::int64_t(std::int32_t(a)) + std::int32_t(b)) >> 1); }},
        {"s rhadd", "as_uint(rhadd(sa, sb))", [](U a, U b, U) { return static_cast<U>((std::int64_t(std::int32_t(a)) + std::int32_t(b) + 1) >> 1); }},
        {"s clamp", "as_uint(clamp(sa, min(sb, sc), max(sb, sc)))", [](U a, U b, U c) {
             const std::int32_t lo = std::min(std::int32_t(b), std::int32_t(c));
             const std::int32_t hi = std::max(std::int32_t(b), std::int32_t(c));
             return static_cast<U>(std::min(std::max(std::int32_t(a), lo), hi));
         }},
        {"s min", "as_uint(min(sa, sb))", [](U a, U b, U) { return static_cast<U>(std::min(std::int32_t(a), std::int32_t(b))); }},
        {"s max", "as_uint(max(sa, sb))", [](U a, U b, U) { return static_cast<U>(std::max(std::int32_t(a), std::int32_t(b))); }},
        {"s div", "as_uint(sa / (int)((b & 0xFFu) + 2u))", [](U a, U b, U) { return static_cast<U>(std::int32_t(a) / std::int32_t((b & 0xFF) + 2)); }},
        {"s mod", "as_uint(sa % (int)((b & 0xFFu) + 2u))", [](U a, U b, U) { return static_cast<U>(std::int32_t(a) % std::int32_t((b & 0xFF) + 2)); }},
        {"s select", "as_uint(select(sa, sb, sc))", [](U a, U b, U c) { return c != 0 ? b : a; }},
        {"s shr", "as_uint(sa >> (int)(b & 31u))", [](U a, U b, U) { return static_cast<U>(std::int32_t(a) >> (b & 31)); }},
    };
    return table;
}

std::string float_kernels() {
    std::string source;
    const std::vector<FloatFunction>& table = float_functions();
    for (std::size_t i = 0; i < table.size(); ++i) {
        source += "__kernel void f_" + std::to_string(i) +
                  "(__global const float* A, __global const float* B, __global const float* C, __global float* O) {\n"
                  "    size_t i = get_global_id(0); float a = A[i], b = B[i], c = C[i];\n    O[i] = " + table[i].expression + ";\n}\n";
    }
    return source;
}

std::string integer_kernel() {
    std::string source =
        "__kernel void lsw_ints(__global const uint* A, __global const uint* B, __global const uint* C, __global uint* O, uint n) {\n"
        "    size_t i = get_global_id(0);\n"
        "    uint a = A[i], b = B[i], c = C[i];\n"
        "    int sa = as_int(a), sb = as_int(b), sc = as_int(c);\n";
    const std::vector<IntOp>& ops = int_ops();
    for (std::size_t k = 0; k < ops.size(); ++k) {
        source += "    O[" + std::to_string(k) + " * n + i] = (uint)(" + ops[k].expression + ");\n";
    }
    source += "}\n";
    return source;
}

struct VectorType {
    const char* name;
    std::size_t bytes;
    bool floating;
};

const VectorType kVectorTypes[] = {{"char", 1, false}, {"uchar", 1, false}, {"short", 2, false}, {"ushort", 2, false}, {"int", 4, false},
                                   {"uint", 4, false}, {"long", 8, false}, {"ulong", 8, false}, {"float", 4, true}};

std::string vector_kernels() {
    std::string source;
    for (const VectorType& type : kVectorTypes) {
        for (const int width : {2, 3, 4, 8, 16}) {
            const std::string vec = std::string(type.name) + std::to_string(width);
            const std::string tag = std::string(type.name) + "_" + std::to_string(width);
            source += "__kernel void vs_" + tag + "(__global const " + type.name + "* in, __global " + type.name + "* out) {\n" +
                      "    size_t i = get_global_id(0);\n" +
                      "    " + vec + " v = vload" + std::to_string(width) + "(i, in);\n" +
                      "    vstore" + std::to_string(width) + "(v * (" + type.name + ")2 + (" + type.name + ")1, i, out);\n}\n";
        }
    }
    return source;
}

void check_floats(ClRig& rig, Context& context, Problems& problems) {
    ClProgram program;
    if (!Kit::compile(rig, program, float_kernels(), "", problems, "float built-ins")) {
        return;
    }
    const std::vector<FloatFunction>& table = float_functions();
    const std::size_t count = 1024;
    std::string worst_name;
    double worst = 0;
    for (std::size_t k = 0; k < table.size(); ++k) {
        const FloatFunction& fn = table[k];
        std::vector<float> a(count);
        std::vector<float> b(count);
        std::vector<float> c(count);
        const std::vector<std::uint32_t> ra = Kit::words(2200 + k, count);
        const std::vector<std::uint32_t> rb = Kit::words(2300 + k, count);
        const std::vector<std::uint32_t> rc = Kit::words(2400 + k, count);
        for (std::size_t i = 0; i < count; ++i) {
            a[i] = static_cast<float>(fn.low + (fn.high - fn.low) * (static_cast<double>(ra[i]) / 4294967296.0));
            b[i] = static_cast<float>(fn.low_b + (fn.high_b - fn.low_b) * (static_cast<double>(rb[i]) / 4294967296.0));
            c[i] = static_cast<float>(-100.0 + 200.0 * (static_cast<double>(rc[i]) / 4294967296.0));
        }
        std::string error;
        ClKernel kernel = program.kernel(("f_" + std::to_string(k)).c_str(), error);
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> ma = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, a.data(), code);
        ClOwned<cl_mem> mb = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, b.data(), code);
        ClOwned<cl_mem> mc = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, c.data(), code);
        ClOwned<cl_mem> mo = Kit::buffer(rig, CL_MEM_WRITE_ONLY, count * 4, nullptr, code);
        if (!kernel || !ma || !mb || !mc || !mo || kernel.set(ma, mb, mc, mo) != CL_SUCCESS) {
            problems.add(std::string(fn.name) + ": " + error + " " + ClTools::error_name(code));
            continue;
        }
        code = kernel.run(count);
        if (code != CL_SUCCESS) {
            problems.add(std::string(fn.name) + ": the kernel returned " + ClTools::error_name(code));
            continue;
        }
        std::vector<float> out(count);
        if (Kit::read(rig, mo.get(), out.data(), count * 4) != CL_SUCCESS) {
            problems.add(std::string(fn.name) + ": reading the results failed");
            continue;
        }
        const double limit = fn.ulp == 0.0 ? 0.51 : fn.ulp;
        double function_worst = 0;
        std::size_t bad = 0;
        std::string detail;
        for (std::size_t i = 0; i < count; ++i) {
            const Real exact = fn.reference(a[i], b[i], c[i]);
            if (!std::isfinite(exact) || (exact != 0 && (std::fabs(exact) < 1e-30L || std::fabs(exact) > 1e30L)) || std::fabs(exact) < static_cast<Real>(fn.guard)) {
                continue;
            }
            const double error_ulps = ClUlp::error(exact, out[i]);
            function_worst = std::max(function_worst, error_ulps);
            if (error_ulps > limit && bad++ == 0) {
                detail = " at (" + std::to_string(a[i]) + ", " + std::to_string(b[i]) + ", " + std::to_string(c[i]) + ") returned " + std::to_string(out[i]) + ", exact " + std::to_string(static_cast<double>(exact)) + ", " + std::to_string(error_ulps) + " ULP";
            }
        }
        if (bad != 0) {
            problems.add(std::string(fn.name) + " exceeds " + std::to_string(fn.ulp) + " ULP for " + std::to_string(bad) + " of " + std::to_string(count) + " inputs, first" + detail);
        }
        if (function_worst > worst) {
            worst = function_worst;
            worst_name = fn.name;
        }
        context.measure(std::string("cl.ulp.") + fn.name, function_worst);
    }
    context.measure("cl.worst_ulp_function", worst_name);
}

void check_integers(ClRig& rig, Context& context, Problems& problems) {
    ClProgram program;
    if (!Kit::compile(rig, program, integer_kernel(), "", problems, "integer built-ins")) {
        return;
    }
    ClKernel kernel = Kit::kernel(program, "lsw_ints", problems);
    if (!kernel) {
        return;
    }
    const std::size_t count = 2048;
    std::vector<std::uint32_t> a = Kit::words(2500, count);
    std::vector<std::uint32_t> b = Kit::words(2501, count);
    std::vector<std::uint32_t> c = Kit::words(2502, count);
    const std::uint32_t edges[] = {0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 2u, 0xFFFFFFFEu, 0x80000001u};
    for (std::size_t i = 0; i < 64; ++i) {
        a[i] = edges[i % 8];
        b[i] = edges[(i / 8) % 8];
        c[i] = edges[(i / 3) % 8];
    }
    const std::vector<IntOp>& ops = int_ops();
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> ma = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, a.data(), code);
    ClOwned<cl_mem> mb = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, b.data(), code);
    ClOwned<cl_mem> mc = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, c.data(), code);
    ClOwned<cl_mem> mo = Kit::buffer(rig, CL_MEM_WRITE_ONLY, count * ops.size() * 4, nullptr, code);
    const cl_uint n = static_cast<cl_uint>(count);
    if (!ma || !mb || !mc || !mo || kernel.set(ma, mb, mc, mo, n) != CL_SUCCESS) {
        problems.add("integer buffers: " + ClTools::error_name(code));
        return;
    }
    code = kernel.run(count);
    std::vector<std::uint32_t> out;
    if (code != CL_SUCCESS) {
        problems.add("the integer kernel returned " + ClTools::error_name(code));
        return;
    }
    if (!Kit::read_words(rig, mo.get(), count * ops.size(), out, problems, "integer results")) {
        return;
    }
    for (std::size_t k = 0; k < ops.size(); ++k) {
        std::size_t bad = 0;
        std::string detail;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t want = ops[k].reference(a[i], b[i], c[i]);
            const std::uint32_t got = out[k * count + i];
            if (got != want && bad++ == 0) {
                detail = " for (0x" + Text::hex(a[i], 8) + ", 0x" + Text::hex(b[i], 8) + ", 0x" + Text::hex(c[i], 8) + ") returned 0x" + Text::hex(got, 8) + ", expected 0x" + Text::hex(want, 8);
            }
        }
        if (bad != 0) {
            problems.add(std::string("integer built-in ") + ops[k].name + " is wrong for " + std::to_string(bad) + " of " + std::to_string(count) + " inputs, first" + detail);
        }
    }
    (void)context;
}

float round_toward(std::int64_t value, int mode) {
    const float nearest = static_cast<float>(value);
    const std::int64_t back = static_cast<std::int64_t>(nearest);
    const bool down = mode == 3 || (mode == 1 && value >= 0);
    const bool up = mode == 2 || (mode == 1 && value < 0);
    if (down && back > value) {
        return std::nextafter(nearest, -INFINITY);
    }
    if (up && back < value) {
        return std::nextafter(nearest, INFINITY);
    }
    return nearest;
}

void check_conversions(ClRig& rig, Context& context, Problems& problems) {
    const char* source =
        "__kernel void lsw_convert(__global const float* f, __global int* o, uint n) {\n"
        "    size_t i = get_global_id(0);\n"
        "    float x = f[i];\n"
        "    o[0 * n + i] = convert_int(x);\n"
        "    o[1 * n + i] = convert_int_rte(x);\n"
        "    o[2 * n + i] = convert_int_rtp(x);\n"
        "    o[3 * n + i] = convert_int_rtn(x);\n"
        "    o[4 * n + i] = convert_int_sat(x);\n"
        "    o[5 * n + i] = (int)convert_uint_sat(x);\n"
        "    o[6 * n + i] = (int)convert_uchar_sat(x);\n"
        "    o[7 * n + i] = (int)convert_char_sat(x);\n"
        "    o[8 * n + i] = (int)convert_short_sat(x);\n"
        "}\n"
        "__kernel void lsw_to_float(__global const int* v, __global float* o, uint n) {\n"
        "    size_t i = get_global_id(0);\n"
        "    int x = v[i];\n"
        "    o[0 * n + i] = convert_float(x);\n"
        "    o[1 * n + i] = convert_float_rtz(x);\n"
        "    o[2 * n + i] = convert_float_rtp(x);\n"
        "    o[3 * n + i] = convert_float_rtn(x);\n"
        "    o[4 * n + i] = convert_float_rte(x);\n"
        "}\n"
        "__kernel void lsw_divide(__global const float* a, __global const float* b, __global float* o) {\n"
        "    size_t i = get_global_id(0); o[i] = a[i] / b[i];\n"
        "}\n";
    ClProgram program;
    if (!Kit::compile(rig, program, source, "", problems, "conversion kernels")) {
        return;
    }
    ClKernel convert = Kit::kernel(program, "lsw_convert", problems);
    ClKernel to_float = Kit::kernel(program, "lsw_to_float", problems);
    ClKernel divide = Kit::kernel(program, "lsw_divide", problems);
    if (!convert || !to_float || !divide) {
        return;
    }
    const std::vector<float> in = {0.f, 0.5f, 1.5f, 2.5f, 3.5f, -0.5f, -1.5f, -2.5f, -3.5f, 2.9f, -2.9f, 2.1f, -2.1f, 100000.7f, -100000.7f, 16777216.f,
                                   1e10f, -1e10f, NAN, INFINITY, -INFINITY, 2147483648.f, -2147483648.f, 2147483520.f, 300.f, -5.f, 255.5f, 127.5f, -128.5f, 40000.f, -40000.f, 70000.f};
    const std::size_t n = in.size();
    const std::size_t in_range = 16;
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> mf = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, n * 4, const_cast<float*>(in.data()), code);
    ClOwned<cl_mem> mo = Kit::buffer(rig, CL_MEM_WRITE_ONLY, n * 9 * 4, nullptr, code);
    const cl_uint count = static_cast<cl_uint>(n);
    if (!mf || !mo || convert.set(mf, mo, count) != CL_SUCCESS || convert.run(n) != CL_SUCCESS) {
        problems.add("the conversion kernel failed: " + ClTools::error_name(code));
        return;
    }
    std::vector<std::int32_t> out(n * 9);
    if (Kit::read(rig, mo.get(), out.data(), out.size() * 4) != CL_SUCCESS) {
        problems.add("reading conversions failed");
        return;
    }
    auto sat = [](double v, double low, double high) -> std::int64_t {
        if (std::isnan(v)) {
            return 0;
        }
        const double t = std::trunc(v);
        return static_cast<std::int64_t>(std::min(std::max(t, low), high));
    };
    for (std::size_t i = 0; i < n; ++i) {
        const double x = static_cast<double>(in[i]);
        const std::string tag = "convert of " + std::to_string(in[i]);
        auto check = [&](const char* what, std::size_t row, std::int64_t want) {
            if (out[row * n + i] != static_cast<std::int32_t>(want)) {
                problems.add(tag + " with " + what + " returned " + std::to_string(out[row * n + i]) + ", expected " + std::to_string(static_cast<std::int32_t>(want)));
            }
        };
        if (i < in_range) {
            check("convert_int (rtz)", 0, static_cast<std::int64_t>(std::trunc(x)));
            check("convert_int_rte", 1, static_cast<std::int64_t>(std::nearbyint(x)));
            check("convert_int_rtp", 2, static_cast<std::int64_t>(std::ceil(x)));
            check("convert_int_rtn", 3, static_cast<std::int64_t>(std::floor(x)));
        }
        check("convert_int_sat", 4, sat(x, -2147483648.0, 2147483647.0));
        check("convert_uint_sat", 5, static_cast<std::int32_t>(static_cast<std::uint32_t>(sat(x, 0.0, 4294967295.0))));
        check("convert_uchar_sat", 6, sat(x, 0.0, 255.0));
        check("convert_char_sat", 7, sat(x, -128.0, 127.0));
        check("convert_short_sat", 8, sat(x, -32768.0, 32767.0));
    }
    const std::vector<std::int32_t> ints = {16777217, -16777217, 2147483647, 1, 123456789, -123456789, 0, 33554435};
    const std::size_t m = ints.size();
    ClOwned<cl_mem> mi = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, m * 4, const_cast<std::int32_t*>(ints.data()), code);
    ClOwned<cl_mem> mfo = Kit::buffer(rig, CL_MEM_WRITE_ONLY, m * 5 * 4, nullptr, code);
    const cl_uint mc = static_cast<cl_uint>(m);
    if (mi && mfo && to_float.set(mi, mfo, mc) == CL_SUCCESS && to_float.run(m) == CL_SUCCESS) {
        std::vector<float> got(m * 5);
        if (Kit::read(rig, mfo.get(), got.data(), got.size() * 4) == CL_SUCCESS) {
            for (std::size_t i = 0; i < m; ++i) {
                const std::int64_t v = ints[i];
                const struct {
                    const char* name;
                    std::size_t row;
                    float want;
                } rows[] = {{"convert_float", 0, round_toward(v, 0)}, {"convert_float_rtz", 1, round_toward(v, 1)},
                            {"convert_float_rtp", 2, round_toward(v, 2)}, {"convert_float_rtn", 3, round_toward(v, 3)}, {"convert_float_rte", 4, round_toward(v, 0)}};
                for (const auto& row : rows) {
                    if (got[row.row * m + i] != row.want) {
                        problems.add(std::string(row.name) + "(" + std::to_string(ints[i]) + ") returned " + std::to_string(got[row.row * m + i]) + ", expected " + std::to_string(row.want));
                    }
                }
            }
        }
    }
    const std::vector<float> num = {1.f, -1.f, 0.f, 5.f, 1.f, -1.f};
    const std::vector<float> den = {0.f, 0.f, 0.f, -0.f, -0.f, -0.f};
    ClOwned<cl_mem> ma = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, num.size() * 4, const_cast<float*>(num.data()), code);
    ClOwned<cl_mem> mb = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, den.size() * 4, const_cast<float*>(den.data()), code);
    ClOwned<cl_mem> md = Kit::buffer(rig, CL_MEM_WRITE_ONLY, num.size() * 4, nullptr, code);
    if (ma && mb && md && divide.set(ma, mb, md) == CL_SUCCESS && divide.run(num.size()) == CL_SUCCESS) {
        std::vector<float> got(num.size());
        if (Kit::read(rig, md.get(), got.data(), got.size() * 4) == CL_SUCCESS) {
            const float want[6] = {INFINITY, -INFINITY, NAN, -INFINITY, -INFINITY, INFINITY};
            for (std::size_t i = 0; i < got.size(); ++i) {
                const bool same = std::isnan(want[i]) ? std::isnan(got[i]) : got[i] == want[i];
                if (!same) {
                    problems.add("float division " + std::to_string(num[i]) + " / " + (std::signbit(den[i]) ? "-0" : "+0") + " returned " + std::to_string(got[i]));
                }
            }
        }
    }
    (void)context;
}

void check_vectors(ClRig& rig, Context& context, Problems& problems) {
    ClProgram program;
    if (!Kit::compile(rig, program, vector_kernels(), "", problems, "vector kernels")) {
        return;
    }
    const std::size_t elements = 97;
    for (const VectorType& type : kVectorTypes) {
        for (const int width : {2, 3, 4, 8, 16}) {
            const std::string tag = std::string(type.name) + std::to_string(width);
            std::string error;
            ClKernel kernel = program.kernel(("vs_" + std::string(type.name) + "_" + std::to_string(width)).c_str(), error);
            if (!kernel) {
                problems.add(tag + ": " + error);
                continue;
            }
            std::vector<std::uint8_t> in(elements * type.bytes);
            std::vector<std::uint8_t> expected(elements * type.bytes, 0x7F);
            const std::size_t groups = elements / static_cast<std::size_t>(width);
            for (std::size_t i = 0; i < elements; ++i) {
                if (type.floating) {
                    const float v = static_cast<float>(static_cast<int>(i % 200) - 100) / 8.0f;
                    std::memcpy(in.data() + i * 4, &v, 4);
                    if (i < groups * static_cast<std::size_t>(width)) {
                        const float r = v * 2.0f + 1.0f;
                        std::memcpy(expected.data() + i * 4, &r, 4);
                    }
                } else {
                    const std::uint64_t v = (i * 7 + 3) % 50;
                    std::memcpy(in.data() + i * type.bytes, &v, type.bytes);
                    if (i < groups * static_cast<std::size_t>(width)) {
                        const std::uint64_t r = v * 2 + 1;
                        std::memcpy(expected.data() + i * type.bytes, &r, type.bytes);
                    }
                }
            }
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> mi = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, in.size(), in.data(), code);
            std::vector<std::uint8_t> sentinel(in.size(), 0x7F);
            ClOwned<cl_mem> mo = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, in.size(), sentinel.data(), code);
            if (!mi || !mo || kernel.set(mi, mo) != CL_SUCCESS || kernel.run(groups) != CL_SUCCESS) {
                problems.add(tag + ": vload/vstore kernel failed " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint8_t> actual(in.size());
            if (Kit::read(rig, mo.get(), actual.data(), actual.size()) == CL_SUCCESS) {
                Kit::same_bytes(context, problems, "vload" + std::to_string(width) + "/vstore" + std::to_string(width) + " of " + type.name, expected.data(), actual.data(), actual.size());
            }
        }
    }
}

}  // namespace

Outcome MathChecks::arithmetic_vs_reference(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        check_integers(rig, context, problems);
        check_floats(rig, context, problems);
        check_conversions(rig, context, problems);
        check_vectors(rig, context, problems);
        return problems.outcome();
    });
}

Outcome MathChecks::doubles_caps(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        cl_device_fp_config config = 0;
        cl_int info_code = CL_SUCCESS;
        const bool queried = rig.info(CL_DEVICE_DOUBLE_FP_CONFIG, config, &info_code);
        if (!queried) {
            config = 0;
        }
        const bool advertised = rig.has_extension("cl_khr_fp64");
        context.measure("cl.double_fp_config", static_cast<unsigned long long>(config));
        if (advertised != (config != 0)) {
            problems.add(std::string("cl_khr_fp64 is ") + (advertised ? "" : "not ") + "listed but CL_DEVICE_DOUBLE_FP_CONFIG is 0x" + Text::hex(config, 4) + " (" + ClTools::error_name(info_code) + ")");
        }
        const char* source =
            "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
            "__kernel void lsw_double(__global const double* a, __global const double* b, __global const double* c, __global double* o, uint n) {\n"
            "    size_t i = get_global_id(0);\n"
            "    double x = a[i], y = b[i], z = c[i];\n"
            "    o[0 * n + i] = x + y;\n"
            "    o[1 * n + i] = x - y;\n"
            "    o[2 * n + i] = x * y;\n"
            "    o[3 * n + i] = x / y;\n"
            "    o[4 * n + i] = sqrt(fabs(x));\n"
            "    o[5 * n + i] = fma(x, y, z);\n"
            "    o[6 * n + i] = exp(x * 0.01);\n"
            "    o[7 * n + i] = log(fabs(x) + 1.0);\n"
            "    o[8 * n + i] = sin(x * 0.1);\n"
            "    o[9 * n + i] = cos(x * 0.1);\n"
            "    o[10 * n + i] = pow(fabs(x) + 0.5, y * 0.01);\n"
            "    o[11 * n + i] = (double)convert_float(x);\n"
            "}\n";
        ClProgram program;
        std::string error;
        if (config == 0) {
            if (program.build(rig, "__kernel void lsw_double(__global double* a) { a[0] = 1.0; }\n", "", error)) {
                problems.add("a kernel using double builds although no double precision is reported");
            }
            if (program.build(rig, source, "", error)) {
                problems.add("a kernel enabling cl_khr_fp64 builds although it is not offered");
            }
            return problems.outcome();
        }
        const cl_device_fp_config minimum = CL_FP_FMA | CL_FP_ROUND_TO_NEAREST | CL_FP_ROUND_TO_ZERO | CL_FP_ROUND_TO_INF | CL_FP_INF_NAN | CL_FP_DENORM;
        if ((config & minimum) != minimum) {
            problems.add("CL_DEVICE_DOUBLE_FP_CONFIG 0x" + Text::hex(config, 4) + " lacks part of the mandated minimum 0x" + Text::hex(minimum, 4));
        }
        if (!program.build(rig, source, "", error)) {
            problems.add("double kernel: " + error);
            return problems.outcome();
        }
        ClKernel kernel = Kit::kernel(program, "lsw_double", problems);
        if (!kernel) {
            return problems.outcome();
        }
        const std::size_t count = 1024;
        std::vector<double> a(count);
        std::vector<double> b(count);
        std::vector<double> c(count);
        const std::vector<std::uint32_t> ra = Kit::words(2600, count);
        const std::vector<std::uint32_t> rb = Kit::words(2601, count);
        const std::vector<std::uint32_t> rc = Kit::words(2602, count);
        for (std::size_t i = 0; i < count; ++i) {
            a[i] = -500.0 + 1000.0 * (static_cast<double>(ra[i]) / 4294967296.0);
            b[i] = 0.5 + 400.0 * (static_cast<double>(rb[i]) / 4294967296.0);
            c[i] = -100.0 + 200.0 * (static_cast<double>(rc[i]) / 4294967296.0);
        }
        a[0] = 4.9406564584124654e-324 * 3;
        b[0] = 2.0;
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> ma = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 8, a.data(), code);
        ClOwned<cl_mem> mb = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 8, b.data(), code);
        ClOwned<cl_mem> mc = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 8, c.data(), code);
        ClOwned<cl_mem> mo = Kit::buffer(rig, CL_MEM_WRITE_ONLY, count * 12 * 8, nullptr, code);
        const cl_uint n = static_cast<cl_uint>(count);
        if (!ma || !mb || !mc || !mo || kernel.set(ma, mb, mc, mo, n) != CL_SUCCESS) {
            problems.add("double buffers: " + ClTools::error_name(code));
            return problems.outcome();
        }
        code = kernel.run(count);
        if (code != CL_SUCCESS) {
            problems.add("the double kernel returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        std::vector<double> out(count * 12);
        if (Kit::read(rig, mo.get(), out.data(), out.size() * 8) != CL_SUCCESS) {
            problems.add("reading the double results failed");
            return problems.outcome();
        }
        const struct {
            const char* name;
            double ulp;
            std::function<Real(Real, Real, Real)> reference;
        } rows[] = {
            {"x + y", 0.0, [](Real x, Real y, Real) { return x + y; }},
            {"x - y", 0.0, [](Real x, Real y, Real) { return x - y; }},
            {"x * y", 0.0, [](Real x, Real y, Real) { return x * y; }},
            {"x / y", 0.0, [](Real x, Real y, Real) { return x / y; }},
            {"sqrt", 0.0, [](Real x, Real, Real) { return std::sqrt(std::fabs(x)); }},
            {"fma", 0.0, [](Real x, Real y, Real z) { return x * y + z; }},
            {"exp", 3.0, [](Real x, Real, Real) { return std::exp(x * 0.01L); }},
            {"log", 3.0, [](Real x, Real, Real) { return std::log(std::fabs(x) + 1.0L); }},
            {"sin", 4.0, [](Real x, Real, Real) { return std::sin(x * 0.1L); }},
            {"cos", 4.0, [](Real x, Real, Real) { return std::cos(x * 0.1L); }},
            {"pow", 16.0, [](Real x, Real y, Real) { return std::pow(std::fabs(x) + 0.5L, y * 0.01L); }},
        };
        for (std::size_t k = 0; k < 11; ++k) {
            std::size_t bad = 0;
            std::string detail;
            double worst = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const Real exact = rows[k].reference(a[i], b[i], c[i]);
                if (!std::isfinite(exact) || (rows[k].ulp > 0 && std::fabs(exact) < 1e-3L && (k == 8 || k == 9))) {
                    continue;
                }
                const double error_ulps = ClUlp::error(exact, out[k * count + i]);
                worst = std::max(worst, error_ulps);
                const double limit = rows[k].ulp == 0.0 ? 0.51 : rows[k].ulp;
                if (error_ulps > limit && bad++ == 0) {
                    detail = " at (" + std::to_string(a[i]) + ", " + std::to_string(b[i]) + ") returned " + std::to_string(out[k * count + i]) + ", " + std::to_string(error_ulps) + " ULP";
                }
            }
            if (bad != 0) {
                problems.add(std::string("double ") + rows[k].name + " exceeds " + std::to_string(rows[k].ulp) + " ULP for " + std::to_string(bad) + " inputs, first" + detail);
            }
            context.measure(std::string("cl.double_ulp.") + rows[k].name, worst);
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (out[11 * count + i] != static_cast<double>(static_cast<float>(a[i])) && std::fabs(a[i]) > 1e-30) {
                problems.add("(double)convert_float(x) differs from the round-to-nearest-even float for x = " + std::to_string(a[i]));
                break;
            }
        }
        if (out[3 * count] != a[0] / b[0] || out[0] != a[0] + b[0]) {
            problems.add("a denormal operand is not handled although CL_FP_DENORM is reported");
        }
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
