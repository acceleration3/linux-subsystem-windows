#include "render/common/capture_dump.hpp"
#include "render/common/case.hpp"
#include "render/common/judge_selftest.hpp"
#include "render/common/png.hpp"
#include "render/common/ref_store.hpp"
#include "render/gl/gl.hpp"
#include "render/platform/gl_platform.hpp"
#include "render/platform/vk_platform.hpp"
#include "render/vk/vk.hpp"

#include <poll.h>
#include <sys/prctl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

extern char** environ;

namespace {

using namespace lsw::render;

constexpr int kExitOk = 0;
constexpr int kExitFail = 1;
constexpr int kExitSkip = 3;

struct Options {
    std::string out;
    std::string gpu = "auto";
    std::vector<std::string> filters;
    int jobs = 0;
    bool list = false;
    bool png = false;
    bool clean = false;
    bool selftest = false;
    std::string run_one;
    std::string self;
};

struct CaseRef {
    std::string id;
    std::string api;
    const CaseSpec* spec = nullptr;
    const CaseTable<vk::Kit>::Entry* vk_entry = nullptr;
    const CaseTable<gl::Kit>::Entry* gl_entry = nullptr;
};

std::vector<std::string> case_problems() {
    std::vector<std::string> out;
    for (const std::string& p : CaseTable<vk::Kit>::instance().problems()) {
        out.push_back("vk: " + p);
    }
    for (const std::string& p : CaseTable<gl::Kit>::instance().problems()) {
        out.push_back("gl: " + p);
    }
    return out;
}

std::vector<CaseRef> all_cases() {
    std::vector<CaseRef> out;
    for (const auto* entry : CaseTable<vk::Kit>::instance().sorted()) {
        out.push_back({entry->spec.id("vk"), "vk", &entry->spec, entry, nullptr});
    }
    for (const auto* entry : CaseTable<gl::Kit>::instance().sorted()) {
        out.push_back({entry->spec.id("gl"), "gl", &entry->spec, nullptr, entry});
    }
    return out;
}

bool glob(const char* pattern, const char* text) {
    if (*pattern == '\0') {
        return *text == '\0';
    }
    if (*pattern == '*') {
        return glob(pattern + 1, text) || (*text != '\0' && glob(pattern, text + 1));
    }
    return *text != '\0' && (*pattern == '?' || *pattern == *text) && glob(pattern + 1, text + 1);
}

bool selected(const Options& options, const std::string& id) {
    bool included = true;
    bool any_include = false;
    for (const std::string& f : options.filters) {
        if (!f.empty() && f[0] == '-') {
            if (glob(f.c_str() + 1, id.c_str())) {
                return false;
            }
        } else {
            any_include = true;
        }
    }
    if (any_include) {
        included = false;
        for (const std::string& f : options.filters) {
            if (!f.empty() && f[0] != '-' && glob(f.c_str(), id.c_str())) {
                included = true;
            }
        }
    }
    return included;
}

std::string hex(const std::uint8_t* bytes, std::size_t count) {
    std::string out;
    char text[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(text, sizeof text, "%02x", bytes[i]);
        out += text;
    }
    return out;
}

struct GpuChoice {
    std::string selector;
    std::string uuid;
    std::string name;
    std::string vendor;
    std::string device;
    std::string driver;
    std::string how;
};

std::vector<std::string> domain_gpus() {
    FILE* pipe = popen("virsh -c qemu:///session dumpxml lsw 2>/dev/null", "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string xml;
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        xml += buffer;
    }
    pclose(pipe);
    std::vector<std::string> found;
    std::size_t device = 0;
    while ((device = xml.find("virtio-gpu-av-pci,id=", device)) != std::string::npos) {
        const std::size_t next = xml.find("virtio-gpu-av-pci,id=", device + 1);
        const std::size_t at = xml.find("host-gpu=", device);
        if (at != std::string::npos && (next == std::string::npos || at < next)) {
            const std::size_t end = xml.find_first_of(",'\"", at);
            if (end != std::string::npos) {
                const std::string uuid = xml.substr(at + 9, end - at - 9);
                if (std::find(found.begin(), found.end(), uuid) == found.end()) {
                    found.push_back(uuid);
                }
            }
        }
        device = next == std::string::npos ? xml.size() : next;
    }
    return found;
}

std::string domain_gpu() {
    const std::vector<std::string> gpus = domain_gpus();
    return gpus.empty() ? std::string() : gpus.front();
}

bool resolve_gpu(const std::string& request, GpuChoice& choice, std::string& error) {
    std::string wanted = request;
    std::string how = request;
    if (request == "auto") {
        const char* env = std::getenv("LSW_RENDER_REF_HOST_GPU");
        std::string from = env != nullptr ? env : domain_gpu();
        if (!from.empty() && from != "auto") {
            wanted = "uuid:" + from;
            how = "auto (host-gpu " + from + (env != nullptr ? " from LSW_RENDER_REF_HOST_GPU" : " of the lsw domain") + ")";
        } else {
            wanted = "index:0";
            how = "auto (first Vulkan device, as the executor binds host-gpu=auto)";
        }
    } else if (request.find(':') == std::string::npos) {
        wanted = request.size() >= 32 ? "uuid:" + request : "index:" + request;
    }
    if (wanted.rfind("uuid:", 0) == 0) {
        std::string raw = wanted.substr(5);
        raw.erase(std::remove(raw.begin(), raw.end(), '-'), raw.end());
        std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        wanted = "uuid:" + raw;
    }
    platform::VkPlatformConfig config;
    config.selector = wanted;
    std::unique_ptr<platform::VkPlatform> host = platform::make_vk_platform(config, error);
    vk::Kit kit;
    vk::KitOptions options;
    if (!host || kit.open(*host, options, error) != OpenResult::Ok) {
        return false;
    }
    choice.selector = "uuid:" + hex(kit.identity.uuid, 16);
    choice.uuid = hex(kit.identity.uuid, 16);
    choice.name = kit.properties.deviceName;
    char text[16];
    std::snprintf(text, sizeof text, "0x%04x", kit.properties.vendorID);
    choice.vendor = text;
    std::snprintf(text, sizeof text, "0x%04x", kit.properties.deviceID);
    choice.device = text;
    std::snprintf(text, sizeof text, "0x%08x", kit.properties.driverVersion);
    choice.driver = text;
    choice.how = how;
    return true;
}

std::string now_text() {
    const std::time_t t = std::time(nullptr);
    char text[40];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return text;
}

int run_one(const Options& options, const CaseRef& ref) {
    RefStore store(options.out);
    store.clear_case(ref.id);
    std::string error;
    platform::VkPlatformConfig vk_config;
    vk_config.selector = options.gpu;
    RunResult result;
    if (ref.vk_entry != nullptr) {
        std::unique_ptr<platform::VkPlatform> host = platform::make_vk_platform(vk_config, error);
        vk::Kit kit;
        if (!host) {
            std::fprintf(stderr, "platform: %s\n", error.c_str());
            return kExitFail;
        }
        const OpenResult opened = kit.open(*host, vk::KitOptions::from(*ref.spec), error);
        if (opened == OpenResult::Failed) {
            std::fprintf(stderr, "kit: %s\n", error.c_str());
            return kExitFail;
        }
        if (opened == OpenResult::Unsupported) {
            result.status = RunStatus::Skip;
            result.reason = error;
        } else {
            std::unique_ptr<Scene<vk::Kit>> scene = ref.vk_entry->make();
            result = run_scene(kit, *ref.spec, *scene);
        }
    } else {
        platform::GlPlatformConfig gl_config;
        gl_config.selector = options.gpu;
        std::unique_ptr<platform::GlPlatform> host = platform::make_gl_platform(gl_config, error);
        gl::Kit kit;
        if (!host) {
            std::fprintf(stderr, "platform: %s\n", error.c_str());
            return kExitFail;
        }
        const OpenResult opened = kit.open(*host, gl::KitOptions::from(*ref.spec), error);
        if (opened == OpenResult::Failed) {
            std::fprintf(stderr, "kit: %s\n", error.c_str());
            return kExitFail;
        }
        if (opened == OpenResult::Unsupported) {
            result.status = RunStatus::Skip;
            result.reason = error;
        } else {
            std::unique_ptr<Scene<gl::Kit>> scene = ref.gl_entry->make();
            result = run_scene(kit, *ref.spec, *scene);
        }
    }
    if (result.status == RunStatus::Skip) {
        if (!store.write_skip(ref.id, result.reason, error) || !store.write_recipe(ref.id, ref.spec->recipe(), error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return kExitFail;
        }
        std::fprintf(stderr, "%s\n", result.reason.c_str());
        return kExitSkip;
    }
    if (result.status == RunStatus::Fail) {
        std::fprintf(stderr, "%s\n", result.reason.c_str());
        return kExitFail;
    }
    std::vector<CaptureRecord> records;
    for (const auto& [frame, set] : result.frames) {
        for (const auto& [label, image] : set.items()) {
            if (options.png) {
                CaptureRecord record;
                record.frame = frame;
                record.label = label;
                record.actual = &image;
                record.tolerance = ref.spec->resolve(label, image.kind());
                records.push_back(record);
            }
            if (!store.write_frame(ref.id, frame, label, image, error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return kExitFail;
            }
            if (options.png) {
                const std::string name = RefStore::frame_name(frame, label);
                static_cast<void>(Png::write(options.out + "/" + ref.id + "/" + name.substr(0, name.size() - 5) + ".png", image));
            }
        }
    }
    if (options.png) {
        static_cast<void>(CaptureDump::write(std::filesystem::path(options.out) / ref.id, ref.id, *ref.spec, records, "host", "ref", false));
    }
    if (!store.write_recipe(ref.id, ref.spec->recipe(), error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return kExitFail;
    }
    return kExitOk;
}

std::string last_line(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    std::string last;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            last = line;
        }
    }
    return last;
}

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

struct Running {
    pid_t pid = 0;
    const CaseRef* ref = nullptr;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point started;
};

int run_all(const Options& options, const std::vector<const CaseRef*>& cases, const GpuChoice& gpu) {
    std::filesystem::create_directories(options.out);
    std::filesystem::create_directories(std::filesystem::path(options.out) / ".logs");
    int jobs = options.jobs > 0 ? options.jobs : static_cast<int>(std::min(16u, std::max(1u, std::thread::hardware_concurrency())));
    std::vector<Running> running;
    std::size_t next = 0;
    int ok = 0;
    int skipped = 0;
    int failed = 0;
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    while (next < cases.size() || !running.empty()) {
        if (g_stop != 0) {
            for (Running& r : running) {
                kill(r.pid, SIGKILL);
                waitpid(r.pid, nullptr, 0);
            }
            std::fprintf(stderr, "lsw-render-ref: interrupted\n");
            return 130;
        }
        while (next < cases.size() && static_cast<int>(running.size()) < jobs) {
            const CaseRef* ref = cases[next++];
            const std::string log = options.out + "/.logs/" + ref->id + ".log";
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            posix_spawn_file_actions_adddup2(&actions, 1, 2);
            std::vector<std::string> args = {options.self, "--out", options.out, "--gpu", gpu.selector, "--run-one", ref->id};
            if (options.png) {
                args.push_back("--png");
            }
            std::vector<char*> argv;
            for (std::string& a : args) {
                argv.push_back(a.data());
            }
            argv.push_back(nullptr);
            pid_t pid = 0;
            if (posix_spawn(&pid, options.self.c_str(), &actions, nullptr, argv.data(), environ) != 0) {
                std::printf("FAIL %s: cannot spawn\n", ref->id.c_str());
                ++failed;
            } else {
                const auto now = std::chrono::steady_clock::now();
                running.push_back({pid, ref, now + std::chrono::seconds(ref->spec->timeout_seconds() + 30), now});
            }
            posix_spawn_file_actions_destroy(&actions);
        }
        bool progressed = false;
        for (std::size_t i = 0; i < running.size();) {
            int status = 0;
            const pid_t done = waitpid(running[i].pid, &status, WNOHANG);
            const bool timed_out = done == 0 && std::chrono::steady_clock::now() > running[i].deadline;
            if (timed_out) {
                kill(running[i].pid, SIGKILL);
                waitpid(running[i].pid, &status, 0);
            }
            if (done == 0 && !timed_out) {
                ++i;
                continue;
            }
            const CaseRef* ref = running[i].ref;
            const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - running[i].started).count();
            const std::string reason = last_line(options.out + "/.logs/" + ref->id + ".log");
            if (timed_out) {
                std::printf("FAIL %s: timed out after %ld ms\n", ref->id.c_str(), ms);
                ++failed;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == kExitOk) {
                std::printf("ok %s %ldms\n", ref->id.c_str(), ms);
                ++ok;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == kExitSkip) {
                std::printf("skip %s: %s\n", ref->id.c_str(), reason.c_str());
                ++skipped;
            } else {
                std::printf("FAIL %s: %s%s\n", ref->id.c_str(), WIFSIGNALED(status) ? "crashed; " : "", reason.c_str());
                ++failed;
            }
            running.erase(running.begin() + static_cast<std::ptrdiff_t>(i));
            progressed = true;
        }
        std::fflush(stdout);
        if (!progressed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    std::printf("lsw-render-ref: %d written, %d skipped, %d failed\n", ok, skipped, failed);
    return failed == 0 ? 0 : 1;
}

int run_every_gpu(const Options& options, int argc, char** argv) {
    const char* env = std::getenv("LSW_RENDER_REF_HOST_GPU");
    std::vector<std::string> uuids;
    if (env != nullptr && *env != '\0') {
        std::stringstream list(env);
        std::string item;
        while (std::getline(list, item, ',')) {
            uuids.push_back(item);
        }
    } else {
        uuids = domain_gpus();
    }
    if (uuids.empty()) {
        std::fprintf(stderr, "lsw-render-ref: --gpu all found no host-gpu= in the lsw domain\n");
        return 2;
    }
    int worst = 0;
    for (const std::string& uuid : uuids) {
        std::vector<std::string> args = {options.self};
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--out" || arg == "--gpu") {
                ++i;
                continue;
            }
            args.push_back(arg);
        }
        args.push_back("--out");
        std::string name = uuid;
        name.erase(std::remove(name.begin(), name.end(), '-'), name.end());
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        args.push_back(options.out + "/" + name);
        args.push_back("--gpu");
        args.push_back(uuid == "auto" ? "auto" : "uuid:" + uuid);
        std::vector<char*> raw;
        for (std::string& a : args) {
            raw.push_back(a.data());
        }
        raw.push_back(nullptr);
        std::printf("lsw-render-ref: gpu %s into %s/%s\n", uuid.c_str(), options.out.c_str(), name.c_str());
        std::fflush(stdout);
        pid_t pid = 0;
        if (posix_spawn(&pid, options.self.c_str(), nullptr, nullptr, raw.data(), environ) != 0) {
            std::printf("FAIL gpu %s: cannot spawn\n", uuid.c_str());
            worst = std::max(worst, 1);
            continue;
        }
        int status = 0;
        waitpid(pid, &status, 0);
        worst = std::max(worst, WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    }
    return worst;
}

void usage() {
    std::puts(
        "usage: lsw-render-ref --out DIR [--gpu UUID|vendor:device|index|auto|all] [--filter GLOB[,GLOB]] [--jobs N] [--list] [--png] [--clean] [--selftest]\n"
        "  --gpu auto reads host-gpu of the lsw libvirt domain (or LSW_RENDER_REF_HOST_GPU), else the first Vulkan device\n"
        "  --gpu all renders every host-gpu= of the domain into OUT/<uuid>/, each with its own device.json");
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    options.self = argv[0];
    char resolved[4096];
    const ssize_t length = readlink("/proc/self/exe", resolved, sizeof resolved - 1);
    if (length > 0) {
        resolved[length] = '\0';
        options.self = resolved;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--out") {
            options.out = value();
        } else if (arg == "--gpu") {
            options.gpu = value();
        } else if (arg == "--filter") {
            std::stringstream list(value());
            std::string item;
            while (std::getline(list, item, ',')) {
                options.filters.push_back(item);
            }
        } else if (arg == "--jobs") {
            options.jobs = std::atoi(value().c_str());
        } else if (arg == "--list") {
            options.list = true;
        } else if (arg == "--png") {
            options.png = true;
        } else if (arg == "--clean") {
            options.clean = true;
        } else if (arg == "--selftest") {
            options.selftest = true;
        } else if (arg == "--run-one") {
            options.run_one = value();
        } else {
            usage();
            return arg == "--help" ? 0 : 2;
        }
    }
    if (options.selftest) {
        std::vector<std::string> passed;
        const std::vector<std::string> failures = JudgeSelfTest::run(passed);
        for (const std::string& line : passed) {
            std::printf("ok %s\n", line.c_str());
        }
        for (const std::string& line : failures) {
            std::printf("FAIL %s\n", line.c_str());
        }
        std::printf("judge selftest: %zu passed, %zu failed\n", passed.size(), failures.size());
        return failures.empty() ? 0 : 1;
    }
    if (const std::vector<std::string> problems = case_problems(); !problems.empty()) {
        for (const std::string& problem : problems) {
            std::fprintf(stderr, "lsw-render-ref: registration error: %s\n", problem.c_str());
        }
        return 2;
    }
    const std::vector<CaseRef> cases = all_cases();
    if (options.list) {
        for (const CaseRef& ref : cases) {
            if (!selected(options, ref.id)) {
                continue;
            }
            std::printf("%s\n", ref.id.c_str());
        }
        return 0;
    }
    if (options.out.empty()) {
        usage();
        return 2;
    }
    if (options.run_one.empty() && options.gpu == "all") {
        return run_every_gpu(options, argc, argv);
    }
    if (!options.run_one.empty()) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        for (const CaseRef& ref : cases) {
            if (ref.id == options.run_one) {
                return run_one(options, ref);
            }
        }
        std::fprintf(stderr, "unknown case %s\n", options.run_one.c_str());
        return kExitFail;
    }
    GpuChoice gpu;
    std::string error;
    if (!resolve_gpu(options.gpu, gpu, error)) {
        std::fprintf(stderr, "lsw-render-ref: cannot select the GPU: %s\n", error.c_str());
        return 2;
    }
    std::printf("lsw-render-ref: gpu %s (vendor %s device %s uuid %s) selected by %s\n", gpu.name.c_str(), gpu.vendor.c_str(),
                gpu.device.c_str(), gpu.uuid.c_str(), gpu.how.c_str());
    DeviceInfo info;
#ifdef LSW_RENDER_REF_REV
    info.generator = LSW_RENDER_REF_REV;
#else
    info.generator = "unknown";
#endif
    info.gpu_name = gpu.name;
    info.vendor_id = gpu.vendor;
    info.device_id = gpu.device;
    info.vulkan_uuid = gpu.uuid;
    info.driver_version = gpu.driver;
    info.created = now_text();
    {
        platform::GlPlatformConfig gl_config;
        gl_config.selector = gpu.selector;
        std::unique_ptr<platform::GlPlatform> host = platform::make_gl_platform(gl_config, error);
        gl::Kit kit;
        if (host && kit.open(*host, gl::KitOptions{}, error) == OpenResult::Ok) {
            info.gl_renderer = kit.renderer;
            info.gl_uuid = kit.uuid;
        } else {
            std::fprintf(stderr, "lsw-render-ref: GL probe failed: %s\n", error.c_str());
        }
    }
    RefStore store(options.out);
    DeviceInfo existing;
    std::string existing_error;
    const bool have_device = store.read_device(existing, existing_error);
    const bool filtered = !options.filters.empty();
    if (options.clean) {
        std::filesystem::remove_all(options.out);
    } else if (have_device) {
        const bool same_gpu = existing.vulkan_uuid == info.vulkan_uuid;
        const bool same_stack = existing.driver_version == info.driver_version && existing.gl_uuid == info.gl_uuid &&
                                existing.gl_renderer == info.gl_renderer;
        if (!same_gpu || (filtered && !same_stack)) {
            std::fprintf(stderr,
                         "lsw-render-ref: %s holds references from %s (uuid %s, driver %s); this run is %s (uuid %s, driver %s). "
                         "Use --clean to replace them.\n",
                         options.out.c_str(), existing.gpu_name.c_str(), existing.vulkan_uuid.c_str(), existing.driver_version.c_str(),
                         info.gpu_name.c_str(), info.vulkan_uuid.c_str(), info.driver_version.c_str());
            return 2;
        }
    }
    if (!filtered) {
        std::filesystem::remove_all(options.out);
    }
    std::filesystem::create_directories(options.out);
    if ((!filtered || !have_device || options.clean) && !store.write_device(info, error)) {
        std::fprintf(stderr, "lsw-render-ref: %s\n", error.c_str());
        return 2;
    }
    std::vector<const CaseRef*> chosen;
    for (const CaseRef& ref : cases) {
        if (selected(options, ref.id)) {
            chosen.push_back(&ref);
        }
    }
    return run_all(options, chosen, gpu);
}
