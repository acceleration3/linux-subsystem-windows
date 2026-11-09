#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "render/host/guest_types.hpp"
#include "render/host/image_judge.hpp"
#include "support/problems.hpp"

#include <memory>
#include <string>

namespace lsw::suites::render {
enum class OpenStatus { Ok, Unsupported, Failed };

class WarpCheck {
public:
    template <class Kit>
    [[nodiscard]] static Outcome run(Context& context, const typename rr::CaseTable<Kit>::Entry& entry) {
        Kit accel;
        Kit oracle;
        std::string error;
        switch (accel.open(false, entry.spec, error)) {
            case OpenStatus::Ok:
                break;
            case OpenStatus::Unsupported:
                return Outcome::skip("accel-virt device: " + error);
            case OpenStatus::Failed:
                return Outcome::fail("device creation on the accel-virt adapter failed: " + error);
        }
        switch (oracle.open(true, entry.spec, error)) {
            case OpenStatus::Ok:
                break;
            case OpenStatus::Unsupported:
                return Outcome::skip("WARP: " + error);
            case OpenStatus::Failed:
                return Outcome::fail("the oracle is unavailable: " + error);
        }
        context.measure("device", accel.name);
        std::unique_ptr<rr::Scene<Kit>> accel_scene = entry.make();
        std::unique_ptr<rr::Scene<Kit>> oracle_scene = entry.make();
        const std::string accel_lacks = accel_scene->unsupported(accel);
        if (!accel_lacks.empty()) {
            return Outcome::skip("accel-virt adapter lacks " + accel_lacks);
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
        ImageJudge::compare_frames(context, problems, entry.spec, expected.frames, actual.frames);
        return problems.outcome();
    }
};
}  // namespace lsw::suites::render
