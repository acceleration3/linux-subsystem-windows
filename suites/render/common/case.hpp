#pragma once

#include "render/common/image.hpp"
#include "render/common/tolerance.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lsw::render {
class CaseSpec {
public:
    struct SameAs {
        std::vector<std::string> siblings;
        std::string reason;
    };

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
        tolerance_set_ = true;
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
    CaseSpec& tolerance_for(const std::string& label, Tolerance value) {
        overrides_[label] = std::move(value);
        return *this;
    }
    CaseSpec& uniform_ok(const std::string& label = "*") {
        uniform_ok_.insert(label);
        return *this;
    }
    CaseSpec& audit_ok(std::string reason) {
        audit_ok_.push_back(std::move(reason));
        return *this;
    }
    CaseSpec& same_as(std::vector<std::string> siblings, std::string reason) {
        same_as_.push_back(SameAs{std::move(siblings), std::move(reason)});
        return *this;
    }
    CaseSpec& min_version(unsigned major, unsigned minor) {
        major_ = major;
        minor_ = minor;
        return *this;
    }
    CaseSpec& extension(std::string name) {
        extensions_.push_back(std::move(name));
        return *this;
    }
    CaseSpec& feature(std::string name) {
        features_.push_back(std::move(name));
        return *this;
    }
    CaseSpec& compat_profile() {
        compat_ = true;
        return *this;
    }
    CaseSpec& source_hash(const char* value) {
        source_hash_ = value;
        return *this;
    }

    [[nodiscard]] const std::string& group() const { return group_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] unsigned width() const { return width_; }
    [[nodiscard]] unsigned height() const { return height_; }
    [[nodiscard]] unsigned frame_count() const { return frames_; }
    [[nodiscard]] const Tolerance& tolerance() const { return tolerance_; }
    [[nodiscard]] bool tolerance_set() const { return tolerance_set_; }
    [[nodiscard]] const std::set<std::string>& uniform_labels() const { return uniform_ok_; }
    [[nodiscard]] const std::vector<std::string>& audit_reasons() const { return audit_ok_; }
    [[nodiscard]] const std::vector<SameAs>& same_as_entries() const { return same_as_; }
    [[nodiscard]] int timeout_seconds() const { return timeout_seconds_; }
    [[nodiscard]] bool uses_swapchain() const { return swapchain_; }
    [[nodiscard]] unsigned variant() const { return variant_; }
    [[nodiscard]] const std::vector<std::string>& tags() const { return tags_; }
    [[nodiscard]] bool multiframe() const { return frames_ > 1; }
    [[nodiscard]] unsigned major() const { return major_; }
    [[nodiscard]] unsigned minor() const { return minor_; }
    [[nodiscard]] const std::vector<std::string>& extensions() const { return extensions_; }
    [[nodiscard]] const std::vector<std::string>& features() const { return features_; }
    [[nodiscard]] bool compat() const { return compat_; }
    [[nodiscard]] const std::vector<unsigned>& capture_list() const { return capture_; }

    [[nodiscard]] bool uniform_allowed(const std::string& label) const {
        return uniform_ok_.count("*") != 0 || uniform_ok_.count(label) != 0;
    }

    [[nodiscard]] Tolerance resolve(const std::string& label, PixelKind kind) const {
        const auto found = overrides_.find(label);
        if (found != overrides_.end()) {
            return in_units_of(found->second, kind);
        }
        if (tolerance_set_) {
            return in_units_of(tolerance_, kind);
        }
        if (PixelInfo::is_integer(kind)) {
            return Tolerance::exact();
        }
        if (PixelInfo::is_float(kind)) {
            const bool depth = kind == PixelKind::Depth32Float || kind == PixelKind::R32Float;
            return depth ? Tolerance::floats(2e-4f, 1e-4f) : Tolerance::floats(4e-3f, 4e-3f);
        }
        return tolerance_;
    }

    [[nodiscard]] static Tolerance in_units_of(Tolerance value, PixelKind kind) {
        if (PixelInfo::is_float(kind) && value.byte_delta > 0) {
            value.absolute = std::max(value.absolute, static_cast<float>(value.byte_delta) / 255.0f);
        }
        return value;
    }

    [[nodiscard]] std::string problem() const {
        if (frames_ == 0) {
            return "frame count is zero";
        }
        if (width_ == 0 || height_ == 0) {
            return "size is zero";
        }
        if (capture_.empty() && frames_ == 0) {
            return "no captured frame";
        }
        for (unsigned frame : capture_) {
            if (frame >= frames_) {
                return "captured frame " + std::to_string(frame) + " is not below the frame count " + std::to_string(frames_);
            }
        }
        return {};
    }

    [[nodiscard]] std::string recipe() const {
        std::string text = group_ + "." + name_ + "|" + std::to_string(width_) + "x" + std::to_string(height_) + "|" +
                           std::to_string(frames_) + "|";
        for (unsigned frame : capture_) {
            text += std::to_string(frame) + ",";
        }
        text += "|" + std::to_string(variant_) + "|" + (swapchain_ ? "s" : "o") + "|" + std::to_string(major_) + "." + std::to_string(minor_) +
                (compat_ ? "c" : "k") + "|";
        for (const std::string& e : extensions_) {
            text += e + ",";
        }
        text += "|";
        for (const std::string& f : features_) {
            text += f + ",";
        }
        text += "|" + source_hash_;
        std::uint64_t hash = 1469598103934665603ull;
        for (unsigned char c : text) {
            hash = (hash ^ c) * 1099511628211ull;
        }
        char out[24];
        std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(hash));
        return out;
    }

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
    bool tolerance_set_ = false;
    int timeout_seconds_ = 60;
    bool swapchain_ = false;
    unsigned variant_ = 0;
    std::vector<std::string> tags_;
    std::map<std::string, Tolerance> overrides_;
    std::set<std::string> uniform_ok_;
    std::vector<std::string> audit_ok_;
    std::vector<SameAs> same_as_;
    unsigned major_ = 0;
    unsigned minor_ = 0;
    std::vector<std::string> extensions_;
    std::vector<std::string> features_;
    bool compat_ = false;
    std::string source_hash_;
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
    [[nodiscard]] virtual bool has_reference() const { return false; }
    [[nodiscard]] virtual bool reference(unsigned, CaptureSet&, std::string&) { return false; }
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

enum class OpenResult { Ok, Unsupported, Failed };

struct RunResult {
    RunStatus status = RunStatus::Ok;
    std::string reason;
    FrameCaptures frames;
};

template <class Kit>
concept HasEndFrame = requires(Kit& kit, std::string& error) {
    { kit.end_frame(error) } -> std::convertible_to<bool>;
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
            if (set.items().empty()) {
                result.status = RunStatus::Fail;
                result.reason = "capture " + std::to_string(f) + " produced no image";
                return result;
            }
            std::set<std::string> labels;
            for (const auto& [label, image] : set.items()) {
                if (image.empty() || !labels.insert(label).second) {
                    result.status = RunStatus::Fail;
                    result.reason = "capture " + std::to_string(f) + (image.empty() ? " has an empty image" : " repeats the label '" + label + "'");
                    return result;
                }
            }
            result.frames[f] = std::move(set);
        }
        if constexpr (HasEndFrame<Kit>) {
            if (spec.multiframe() && !kit.end_frame(error)) {
                result.status = RunStatus::Fail;
                result.reason = "end of frame " + std::to_string(f) + ": " + error;
                return result;
            }
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
        const std::string key = spec.group() + "." + spec.name();
        const std::string why = spec.problem();
        if (!why.empty()) {
            problems_.push_back("case " + key + ": " + why);
            return;
        }
        if (!keys_.insert(key).second) {
            problems_.push_back("duplicate case " + key);
            return;
        }
        entries_.push_back(Entry{std::move(spec), std::move(make)});
    }

    [[nodiscard]] std::vector<const Entry*> sorted() const {
        std::vector<const Entry*> out;
        for (const Entry& entry : entries_) {
            out.push_back(&entry);
        }
        std::stable_sort(out.begin(), out.end(), [](const Entry* a, const Entry* b) {
            return a->spec.group() != b->spec.group() ? a->spec.group() < b->spec.group() : a->spec.name() < b->spec.name();
        });
        return out;
    }

    [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

private:
    std::vector<Entry> entries_;
    std::set<std::string> keys_;
    std::vector<std::string> problems_;
};

#ifndef LSW_RENDER_SOURCE_HASH
#define LSW_RENDER_SOURCE_HASH "unhashed"
#endif

template <class Kit, class SceneType>
class Register {
public:
    explicit Register(CaseSpec spec, const char* source_hash = LSW_RENDER_SOURCE_HASH) {
        spec.source_hash(source_hash);
        CaseTable<Kit>::instance().add(std::move(spec), [] { return std::unique_ptr<Scene<Kit>>(new SceneType()); });
    }
};
}  // namespace lsw::render
