#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "render/host/guest_types.hpp"
#include "render/host/image_judge.hpp"
#include "support/problems.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace lsw::suites::render {
enum class OpenStatus { Ok, Unsupported, Failed };

class WarpCheck {
public:
    [[nodiscard]] static bool warp_under_test() {
        const char* env = std::getenv("LSW_RENDER_DUT");
        return env != nullptr && std::strcmp(env, "warp") == 0;
    }

    template <class Kit>
    [[nodiscard]] static Outcome run(Context& context, const typename rr::CaseTable<Kit>::Entry& entry) {
        Kit accel;
        Kit oracle;
        std::string error;
        const bool self = warp_under_test();
        context.phase("open device side");
        switch (accel.open(self, entry.spec, error)) {
            case OpenStatus::Ok:
                break;
            case OpenStatus::Unsupported:
                return Outcome::skip((self ? "WARP device under test: " : "accel-virt device: ") + error);
            case OpenStatus::Failed:
                return Outcome::fail((self ? "device creation on the WARP device under test failed: " : "device creation on the accel-virt adapter failed: ") + error);
        }
        std::unique_ptr<rr::Scene<Kit>> oracle_scene = entry.make();
        const bool analytic = oracle_scene->has_reference();
        if (!analytic) {
            context.phase("open oracle side");
            switch (oracle.open(true, entry.spec, error)) {
                case OpenStatus::Ok:
                    break;
                case OpenStatus::Unsupported:
                    return Outcome::skip("WARP: " + error);
                case OpenStatus::Failed:
                    return Outcome::fail("the oracle is unavailable: " + error);
            }
        }
        if constexpr (requires { accel.under_test = true; }) {
            accel.under_test = true;
        }
        context.measure("device", self ? accel.name + " (device under test)" : accel.name);
        if (self) {
            context.measure("dut", "warp");
        }
        std::unique_ptr<rr::Scene<Kit>> accel_scene = entry.make();
        context.phase("device side setup");
        const std::string accel_lacks = accel_scene->unsupported(accel);
        if (!accel_lacks.empty()) {
            return Outcome::skip((self ? "the WARP device under test lacks " : "accel-virt adapter lacks ") + accel_lacks);
        }
        rr::RunResult expected;
        if (analytic) {
            context.phase("analytic reference");
            for (unsigned f = 0; f < entry.spec.frame_count(); ++f) {
                if (!entry.spec.captures(f)) {
                    continue;
                }
                rr::CaptureSet set;
                if (!oracle_scene->reference(accel, f, set, error)) {
                    return Outcome::fail("the analytic reference failed: " + error);
                }
                expected.frames[f] = std::move(set);
            }
        } else {
            const std::string oracle_lacks = oracle_scene->unsupported(oracle);
            if (!oracle_lacks.empty()) {
                return Outcome::skip("WARP lacks " + oracle_lacks);
            }
            context.phase("oracle side running");
            expected = rr::run_scene(oracle, entry.spec, *oracle_scene);
            if (expected.status != rr::RunStatus::Ok) {
                return Outcome::fail("the WARP oracle failed: " + expected.reason);
            }
        }
        context.phase("device side running");
        rr::RunResult actual = rr::run_scene(accel, entry.spec, *accel_scene);
        if (actual.status != rr::RunStatus::Ok) {
            return Outcome::fail(actual.reason);
        }
        rr::RunResult low;
        rr::RunResult high;
        const bool bracket = entry.spec.lod_bracket_bias() > 0.0f && !analytic && !self;
        if (bracket) {
            context.measure("lod_bracket", std::string(rr::CaseSpec::lod_bracket_reason()));
            const float bias = entry.spec.lod_bracket_bias();
            for (int side = 0; side < 2; ++side) {
                Kit shifted;
                if (shifted.open(true, entry.spec, error) != OpenStatus::Ok) {
                    return Outcome::fail("the WARP bracket oracle is unavailable: " + error);
                }
                if constexpr (requires { shifted.lod_bias = 0.0f; }) {
                    shifted.lod_bias = side == 0 ? -bias : bias;
                }
                std::unique_ptr<rr::Scene<Kit>> shifted_scene = entry.make();
                context.phase(side == 0 ? "bracket oracle low" : "bracket oracle high");
                (side == 0 ? low : high) = rr::run_scene(shifted, entry.spec, *shifted_scene);
                if ((side == 0 ? low : high).status != rr::RunStatus::Ok) {
                    return Outcome::fail("the WARP bracket oracle failed: " + (side == 0 ? low : high).reason);
                }
            }
        }
        context.phase("judging");
        Problems problems;
        ImageJudge::compare_frames(context, problems, entry.spec, expected.frames, actual.frames, true, bracket ? &low.frames : nullptr,
                                   bracket ? &high.frames : nullptr);
        return problems.outcome();
    }
};
}  // namespace lsw::suites::render
