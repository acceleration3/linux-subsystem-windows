#include "sanity/present/kit.hpp"

#include "sanity/interop/kit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace lsw::suites::sanity::present {

Opened Kit::open(PresentPath& path, int width, int height, std::string& error) {
    if (path.open(kX, kY, width, height, error)) {
        return Opened::Yes;
    }
    path.close();
    if (error.rfind("unsupported: ", 0) == 0) {
        error = error.substr(13);
        return Opened::Unsupported;
    }
    return Opened::Failed;
}

bool Kit::await_frame_in(const RECT& area, int frame, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        if (FrameProbe::sample(area) == frame) {
            return true;
        }
        ::Sleep(2);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool Kit::await_frame(const PresentPath& path, int frame, int timeout_ms) {
    return await_frame_in(path.client(), frame, timeout_ms);
}

bool Kit::settle(Context& context, const std::string& label, const PresentPath& path, const std::vector<std::uint32_t>& expected, int tolerance,
                 std::string& detail) {
    return interop::Kit::settle_screen(context, label, path.client(), expected, tolerance, 3000, detail);
}

std::string Kit::hr(HRESULT result) {
    return interop::Kit::hr(result);
}

std::string Kit::sanitize(std::string text) {
    return interop::Kit::sanitize(std::move(text));
}

void Kit::report_status(Context& context, const std::string& id, const std::string& what, int occluded, int total) {
    context.measure(id + "." + what + ".occluded", occluded);
    context.measure(id + "." + what + ".presents", total);
}

Kit::Cadence Kit::cadence(const std::vector<long long>& stamps, std::size_t skip, double expected_ms) {
    Cadence out;
    if (stamps.size() <= skip + 1) {
        return out;
    }
    out.first_qpc = stamps[skip];
    out.last_qpc = stamps.back();
    for (std::size_t i = skip + 1; i < stamps.size(); ++i) {
        out.periods_ms.push_back(QpcClock::ms(stamps[i] - stamps[i - 1]));
    }
    double sum = 0.0;
    for (const double period : out.periods_ms) {
        sum += period;
        out.longest = std::max(out.longest, period);
    }
    out.mean = sum / static_cast<double>(out.periods_ms.size());
    double squares = 0.0;
    std::size_t outside = 0;
    for (const double period : out.periods_ms) {
        squares += (period - out.mean) * (period - out.mean);
        if (period < expected_ms * 0.5 || period > expected_ms * 1.5) {
            ++outside;
        }
    }
    out.deviation = std::sqrt(squares / static_cast<double>(out.periods_ms.size()));
    out.outside_fraction = static_cast<double>(outside) / static_cast<double>(out.periods_ms.size());
    return out;
}

}  // namespace lsw::suites::sanity::present
