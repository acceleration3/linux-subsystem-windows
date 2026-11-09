#include "render/host/ref_check.hpp"

#include "render/host/image_judge.hpp"
#include "support/problems.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace lsw::suites::render {

std::filesystem::path RefCheck::root() {
    const char* env = std::getenv("LSW_SUITES_REFS");
    if (env != nullptr && *env != '\0') {
        return env;
    }
    wchar_t path[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path exe(std::wstring(path, length));
    return exe.parent_path() / "render-refs";
}

std::string RefCheck::hex(const unsigned char* bytes, std::size_t count) {
    std::string out;
    char text[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(text, sizeof text, "%02x", bytes[i]);
        out += text;
    }
    return out;
}

bool RefCheck::open_store(rr::RefStore& store, std::string& error) {
    if (!store.exists()) {
        error = "no host references at " + store.root().string();
        return false;
    }
    return true;
}

bool RefCheck::device_matches(const rr::RefStore& store, const std::string& guest_vulkan_uuid, const std::string& guest_gl_uuid,
                              const std::string& guest_name, std::string& error) {
    rr::DeviceInfo info;
    if (!store.read_device(info, error)) {
        return false;
    }
    if (!guest_vulkan_uuid.empty() && info.vulkan_uuid != guest_vulkan_uuid) {
        error = "the references were rendered on " + info.gpu_name + " (vulkan uuid " + info.vulkan_uuid +
                ") but the guest runs on " + guest_name + " (vulkan uuid " + guest_vulkan_uuid + ")";
        return false;
    }
    if (!guest_gl_uuid.empty() && !info.gl_uuid.empty() && info.gl_uuid != guest_gl_uuid) {
        error = "the references were rendered on " + info.gpu_name + " (gl uuid " + info.gl_uuid + ") but the guest GL runs on " +
                guest_name + " (gl uuid " + guest_gl_uuid + ")";
        return false;
    }
    return true;
}

Outcome RefCheck::judge(Context& context, const std::string& id, const rr::CaseSpec& spec, const rr::FrameCaptures& frames,
                        const rr::RefStore& store) {
    Problems problems;
    rr::FrameCaptures expected;
    for (const auto& [frame, set] : frames) {
        rr::CaptureSet loaded;
        for (const auto& [capture, image] : set.items()) {
            rr::Image reference;
            std::string error;
            if (!store.read_frame(id, frame, capture, reference, error)) {
                problems.add(error);
                continue;
            }
            loaded.add(capture, std::move(reference));
        }
        expected[frame] = std::move(loaded);
    }
    if (!problems.empty()) {
        return problems.outcome();
    }
    ImageJudge::compare_frames(context, problems, expected, frames, spec.tolerance());
    return problems.outcome();
}

}  // namespace lsw::suites::render
