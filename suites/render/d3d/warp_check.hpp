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
        switch (accel.open(self, entry.spec, error)) {
            case OpenStatus::Ok:
                break;
            case OpenStatus::Unsupported:
                return Outcome::skip((self ? "WARP device under test: " : "accel-virt device: ") + error);
            case OpenStatus::Failed:
                return Outcome::fail((self ? "device creation on the WARP device under test failed: " : "device creation on the accel-virt adapter failed: ") + error);
        }
        switch (oracle.open(true, entry.spec, error)) {
            case OpenStatus::Ok:
                break;
            case OpenStatus::Unsupported:
                return Outcome::skip("WARP: " + error);
            case OpenStatus::Failed:
                return Outcome::fail("the oracle is unavailable: " + error);
        }
        context.measure("device", self ? accel.name + " (device under test)" : accel.name);
        if (self) {
            context.measure("dut", "warp");
        }
        std::unique_ptr<rr::Scene<Kit>> accel_scene = entry.make();
        std::unique_ptr<rr::Scene<Kit>> oracle_scene = entry.make();
        const std::string accel_lacks = accel_scene->unsupported(accel);
        if (!accel_lacks.empty()) {
            return Outcome::skip((self ? "the WARP device under test lacks " : "accel-virt adapter lacks ") + accel_lacks);
        }
        const std::string oracle_lacks = oracle_scene->unsupported(oracle);
        if (!oracle_lacks.empty()) {
            return Outcome::skip("WARP lacks " + oracle_lacks);
        }
        rr::RunResult expected = rr::run_scene(oracle, entry.spec, *oracle_scene);
        if (expected.status != rr::RunStatus::Ok) {
            return Outcome::fail("the WARP oracle failed: " + expected.reason);
        }
        rr::RunResult actual = rr::run_scene(accel, entry.spec, *accel_scene);
        if (actual.status != rr::RunStatus::Ok) {
            return Outcome::fail(actual.reason);
        }
        Problems problems;
        ImageJudge::compare_frames(context, problems, entry.spec, expected.frames, actual.frames, true);
        return problems.outcome();
    }
};
}  // namespace lsw::suites::render
