#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "support/frame_image.hpp"
#include "support/frame_probe.hpp"
#include "support/frame_watcher.hpp"
#include "support/present_path.hpp"
#include "support/problems.hpp"
#include "support/qpc_clock.hpp"
#include "support/refresh_rate.hpp"
#include "support/screen_capture.hpp"
#include "support/sequence_judge.hpp"

#include <string>
#include <vector>

namespace lsw::suites::sanity::present {
enum class Opened { Yes, Unsupported, Failed };

class Kit {
public:
    static constexpr int kX = 60;
    static constexpr int kY = 60;

    [[nodiscard]] static Opened open(PresentPath& path, int width, int height, std::string& error);

    [[nodiscard]] static bool await_frame(const PresentPath& path, int frame, int timeout_ms);

    [[nodiscard]] static bool await_frame_in(const RECT& area, int frame, int timeout_ms);

    [[nodiscard]] static bool settle(Context& context, const std::string& label, const PresentPath& path, const std::vector<std::uint32_t>& expected,
                                     int tolerance, std::string& detail);

    [[nodiscard]] static std::string hr(HRESULT result);

    [[nodiscard]] static std::string label(const std::string& id, const std::string& text) { return id + ": " + text; }

    [[nodiscard]] static std::string sanitize(std::string text);

    static void report_status(Context& context, const std::string& id, const std::string& what, int occluded, int total);

    struct Cadence {
        std::vector<double> periods_ms;
        double mean = 0.0;
        double deviation = 0.0;
        double longest = 0.0;
        double outside_fraction = 0.0;
        long long first_qpc = 0;
        long long last_qpc = 0;
    };

    [[nodiscard]] static Cadence cadence(const std::vector<long long>& stamps, std::size_t skip, double expected_ms);
};
}  // namespace lsw::suites::sanity::present
