#pragma once

#include "render/common/image.hpp"
#include "render/common/tolerance.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lsw::render {
class CaseSpec {
public:
    CaseSpec(std::string group, std::string name) : group_(std::move(group)), name_(std::move(name)) {}

    CaseSpec& size(unsigned width, unsigned height) {
        width_ = width;
        height_ = height;
        return *this;
    }
    CaseSpec& frames(unsigned count, std::vector<unsigned> captured) {
        frames_ = count;
        capture_ = std::move(captured);
        return *this;
    }
    CaseSpec& tolerance(Tolerance value) {
        tolerance_ = std::move(value);
        return *this;
    }
    CaseSpec& timeout(int seconds) {
        timeout_seconds_ = seconds;
        return *this;
    }
    CaseSpec& swapchain() {
        swapchain_ = true;
        return *this;
    }
    CaseSpec& variant(unsigned value) {
        variant_ = value;
        return *this;
    }
    CaseSpec& tag(std::string value) {
        tags_.push_back(std::move(value));
        return *this;
    }

    [[nodiscard]] const std::string& group() const { return group_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] unsigned width() const { return width_; }
    [[nodiscard]] unsigned height() const { return height_; }
    [[nodiscard]] unsigned frame_count() const { return frames_; }
    [[nodiscard]] const Tolerance& tolerance() const { return tolerance_; }
    [[nodiscard]] int timeout_seconds() const { return timeout_seconds_; }
    [[nodiscard]] bool uses_swapchain() const { return swapchain_; }
    [[nodiscard]] unsigned variant() const { return variant_; }
    [[nodiscard]] const std::vector<std::string>& tags() const { return tags_; }
    [[nodiscard]] bool multiframe() const { return frames_ > 1; }

    [[nodiscard]] bool captures(unsigned frame) const {
        if (capture_.empty()) {
            return frame + 1 == frames_;
        }
        return std::find(capture_.begin(), capture_.end(), frame) != capture_.end();
    }

    [[nodiscard]] std::string id(const std::string& api) const { return "render." + api + "." + group_ + "." + name_; }

private:
    std::string group_;
    std::string name_;
    unsigned width_ = 128;
    unsigned height_ = 128;
    unsigned frames_ = 1;
    std::vector<unsigned> capture_;
    Tolerance tolerance_ = Tolerance::tight();
    int timeout_seconds_ = 60;
    bool swapchain_ = false;
    unsigned variant_ = 0;
    std::vector<std::string> tags_;
};

class CaptureSet {
public:
    void add(std::string label, Image image) { items_.emplace_back(std::move(label), std::move(image)); }
    [[nodiscard]] const std::vector<std::pair<std::string, Image>>& items() const { return items_; }

private:
    std::vector<std::pair<std::string, Image>> items_;
};

using FrameCaptures = std::map<unsigned, CaptureSet>;

template <class Kit>
class Scene {
public:
    virtual ~Scene() = default;
    [[nodiscard]] virtual std::string unsupported(Kit&) { return {}; }
    [[nodiscard]] virtual bool setup(Kit&, std::string&) { return true; }
    [[nodiscard]] virtual bool frame(Kit&, unsigned, std::string&) { return true; }
    [[nodiscard]] virtual bool capture(Kit&, unsigned, CaptureSet&, std::string&) = 0;
};

template <class Kit>
class SingleScene : public Scene<Kit> {
public:
    [[nodiscard]] virtual bool draw(Kit& kit, CaptureSet& out, std::string& error) = 0;

    [[nodiscard]] bool capture(Kit& kit, unsigned, CaptureSet& out, std::string& error) final { return draw(kit, out, error); }
};

enum class RunStatus { Ok, Skip, Fail };

struct RunResult {
    RunStatus status = RunStatus::Ok;
    std::string reason;
    FrameCaptures frames;
};

template <class Kit>
RunResult run_scene(Kit& kit, const CaseSpec& spec, Scene<Kit>& scene) {
    RunResult result;
    std::string skip = scene.unsupported(kit);
    if (!skip.empty()) {
        result.status = RunStatus::Skip;
        result.reason = std::move(skip);
        return result;
    }
    std::string error;
    if (!scene.setup(kit, error)) {
        result.status = RunStatus::Fail;
        result.reason = "setup: " + error;
        return result;
    }
    for (unsigned f = 0; f < spec.frame_count(); ++f) {
        if (!scene.frame(kit, f, error)) {
            result.status = RunStatus::Fail;
            result.reason = "frame " + std::to_string(f) + ": " + error;
            return result;
        }
        if (spec.captures(f)) {
            CaptureSet set;
            if (!scene.capture(kit, f, set, error)) {
                result.status = RunStatus::Fail;
                result.reason = "capture " + std::to_string(f) + ": " + error;
                return result;
            }
            result.frames[f] = std::move(set);
        }
    }
    return result;
}

template <class Kit>
class CaseTable {
public:
    struct Entry {
        CaseSpec spec;
        std::function<std::unique_ptr<Scene<Kit>>()> make;
    };

    [[nodiscard]] static CaseTable& instance() {
        static CaseTable table;
        return table;
    }

    void add(CaseSpec spec, std::function<std::unique_ptr<Scene<Kit>>()> make) {
        entries_.push_back(Entry{std::move(spec), std::move(make)});
    }

    [[nodiscard]] std::vector<const Entry*> sorted() const {
        std::vector<const Entry*> out;
        for (const Entry& entry : entries_) {
            out.push_back(&entry);
        }
        std::sort(out.begin(), out.end(), [](const Entry* a, const Entry* b) {
            return a->spec.group() != b->spec.group() ? a->spec.group() < b->spec.group() : a->spec.name() < b->spec.name();
        });
        return out;
    }

private:
    std::vector<Entry> entries_;
};

template <class Kit, class SceneType>
class Register {
public:
    explicit Register(CaseSpec spec) {
        CaseTable<Kit>::instance().add(std::move(spec), [] { return std::unique_ptr<Scene<Kit>>(new SceneType()); });
    }
};
}  // namespace lsw::render
