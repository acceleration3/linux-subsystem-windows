#include "render/vk/quadkit.hpp"

#include <cmath>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

struct CompareEntry {
    const char* name;
    VkCompareOp op;
};

const std::vector<CompareEntry>& compare_ops() {
    static const std::vector<CompareEntry> table = {
        {"never", VK_COMPARE_OP_NEVER},       {"less", VK_COMPARE_OP_LESS},
        {"equal", VK_COMPARE_OP_EQUAL},       {"less_or_equal", VK_COMPARE_OP_LESS_OR_EQUAL},
        {"greater", VK_COMPARE_OP_GREATER},   {"not_equal", VK_COMPARE_OP_NOT_EQUAL},
        {"greater_or_equal", VK_COMPARE_OP_GREATER_OR_EQUAL}, {"always", VK_COMPARE_OP_ALWAYS},
    };
    return table;
}

struct StencilOpEntry {
    const char* name;
    VkStencilOp op;
};

const std::vector<StencilOpEntry>& stencil_ops() {
    static const std::vector<StencilOpEntry> table = {
        {"keep", VK_STENCIL_OP_KEEP},
        {"zero", VK_STENCIL_OP_ZERO},
        {"replace", VK_STENCIL_OP_REPLACE},
        {"increment_clamp", VK_STENCIL_OP_INCREMENT_AND_CLAMP},
        {"decrement_clamp", VK_STENCIL_OP_DECREMENT_AND_CLAMP},
        {"invert", VK_STENCIL_OP_INVERT},
        {"increment_wrap", VK_STENCIL_OP_INCREMENT_AND_WRAP},
        {"decrement_wrap", VK_STENCIL_OP_DECREMENT_AND_WRAP},
    };
    return table;
}

void capture_depth_and_color(Rig& rig, Target& target, CaptureSet& out, Image color, bool depth, bool stencil, bool with_color = true) {
    if (with_color) {
        out.add("color", std::move(color));
    }
    if (depth) {
        out.add("depth", [&] {
            Image image;
            std::string error;
            rig.must(target.read_depth(rig.kit, image, error), error);
            return image;
        }());
    }
    if (stencil) {
        out.add("stencil", [&] {
            Image image;
            std::string error;
            rig.must(target.read_stencil(rig.kit, image, error), error);
            return image;
        }());
    }
}

void register_compare() {
    struct DepthFormat {
        const char* suffix;
        VkFormat format;
        bool stencil;
        const char* twin;
        const char* twin_reason;
    };
    const std::vector<DepthFormat> formats = {
        {"d16", VK_FORMAT_D16_UNORM, false, "d16_s8", "colour and depth planes equal the d16 s8 row, whose stencil plane marks the passing columns"},
        {"d16_s8", VK_FORMAT_D16_UNORM_S8_UINT, true, "d16", "colour and depth planes equal the d16 row; the stencil plane here marks the passing columns"},
        {"x8_d24", VK_FORMAT_X8_D24_UNORM_PACK32, false, "d24_s8", "colour and depth planes equal the d24 s8 row, whose stencil plane marks the passing columns"},
        {"d24_s8", VK_FORMAT_D24_UNORM_S8_UINT, true, "x8_d24", "colour and depth planes equal the x8 d24 row; the stencil plane here marks the passing columns"},
        {"d32", VK_FORMAT_D32_SFLOAT, false, "d32_s8", "colour and depth planes equal the d32 s8 row, whose stencil plane marks the passing columns"},
        {"d32_s8", VK_FORMAT_D32_SFLOAT_S8_UINT, true, "d32", "colour and depth planes equal the d32 row; the stencil plane here marks the passing columns"},
    };
    for (const DepthFormat& depth_format : formats) {
    for (const CompareEntry& entry : compare_ops()) {
        if (entry.op == VK_COMPARE_OP_NEVER && depth_format.format != VK_FORMAT_D32_SFLOAT) {
            continue;
        }
        CaseSpec spec("om", std::string("depth_compare_") + entry.name + "_" + depth_format.suffix);
        spec.tolerance(Tolerance::exact());
        if (entry.op == VK_COMPARE_OP_ALWAYS) {
            std::vector<std::string> others;
            for (const DepthFormat& other : formats) {
                if (other.format != depth_format.format) {
                    others.push_back(std::string("om.depth_compare_always_") + other.suffix);
                }
            }
            spec.same_as(others, "always passes every fragment, so the colour and stencil planes do not depend on the depth format; the depth plane holds each format's quantization of the seven depths");
        } else if (entry.op != VK_COMPARE_OP_NEVER) {
            spec.same_as({std::string("om.depth_compare_") + entry.name + "_" + depth_format.twin}, depth_format.twin_reason);
        }
        Cases::add(std::move(spec), [entry, depth_format](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = depth_format.format;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc base = quads.desc(target);
            base.depth_test = true;
            base.depth_op = VK_COMPARE_OP_ALWAYS;
            PipelineDesc test = quads.desc(target);
            test.depth_test = true;
            test.depth_op = entry.op;
            if (depth_format.stencil) {
                test.stencil_test = true;
                test.stencil.compareOp = VK_COMPARE_OP_ALWAYS;
                test.stencil.passOp = VK_STENCIL_OP_REPLACE;
                test.stencil.failOp = VK_STENCIL_OP_KEEP;
                test.stencil.depthFailOp = VK_STENCIL_OP_KEEP;
                test.stencil.compareMask = 0xff;
                test.stencil.writeMask = 0xff;
                test.stencil.reference = 0x81;
            }
            const VkPipeline p_base = rig.pipe(base);
            const VkPipeline p_test = rig.pipe(test);
            const float z0 = 0.2f;
            const float d16 = 1.0f / 65535.0f;
            const float zs[7] = {z0 - 1.5f * d16, z0 - 0.2f * d16, std::nextafter(z0, 0.0f), z0, std::nextafter(z0, 1.0f), z0 + 0.2f * d16, z0 + 1.5f * d16};
            Image color = rig.draw(target, p_base, nullptr, [&](VkCommandBuffer cb) {
                for (unsigned i = 0; i < 7; ++i) {
                    const float grey[4] = {0.3f + 0.05f * static_cast<float>(i), 0.3f, 0.5f - 0.04f * static_cast<float>(i), 1.0f};
                    quads.quad(cb, -1.0f + 2.0f / 7.0f * static_cast<float>(i), -1, 2.0f / 7.0f, 2, grey, z0);
                }
                rig.bind_graphics(cb, p_test);
                for (unsigned i = 0; i < 7; ++i) {
                    quads.quad(cb, -0.96f + 2.0f / 7.0f * static_cast<float>(i), -0.7f, 0.22f, 1.4f, palette(i), zs[i]);
                }
            }, quads.layout);
            const bool observable = entry.op != VK_COMPARE_OP_NEVER;
            const bool depth_differs = entry.op == VK_COMPARE_OP_LESS || entry.op == VK_COMPARE_OP_GREATER || entry.op == VK_COMPARE_OP_ALWAYS;
            capture_depth_and_color(rig, target, out, std::move(color), depth_differs, observable && depth_format.stencil);
        }, [depth_format](Kit& kit) { return Cases::need_format(kit, depth_format.format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "depth format"); });
    }
    }
}

void register_formats() {
    struct Entry {
        const char* name;
        VkFormat format;
    };
    const std::vector<Entry> formats = {
        {"d16", VK_FORMAT_D16_UNORM},         {"x8_d24", VK_FORMAT_X8_D24_UNORM_PACK32}, {"d32", VK_FORMAT_D32_SFLOAT},
        {"d16_s8", VK_FORMAT_D16_UNORM_S8_UINT}, {"d24_s8", VK_FORMAT_D24_UNORM_S8_UINT}, {"d32_s8", VK_FORMAT_D32_SFLOAT_S8_UINT},
    };
    for (const Entry& entry : formats) {
        CaseSpec spec("om", std::string("depth_format_") + entry.name);
        spec.tolerance(Tolerance::exact());
        if (entry.format == VK_FORMAT_D16_UNORM_S8_UINT || entry.format == VK_FORMAT_D24_UNORM_S8_UINT || entry.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            spec.uniform_ok("stencil");
        }
        if (entry.format == VK_FORMAT_D16_UNORM) {
            spec.same_as({"om.depth_format_d16_s8"}, "the depth plane equals the one of the d16 s8 row, whose stencil plane is the difference");
        } else if (entry.format == VK_FORMAT_D16_UNORM_S8_UINT) {
            spec.same_as({"om.depth_format_d16"}, "the depth plane equals the one of the d16 row; the stencil plane holds the clear value 0x5a");
        } else if (entry.format == VK_FORMAT_D32_SFLOAT) {
            spec.same_as({"om.depth_format_d32_s8"}, "the depth plane equals the one of the d32 s8 row, whose stencil plane is the difference");
        } else if (entry.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            spec.same_as({"om.depth_format_d32"}, "the depth plane equals the one of the d32 row; the stencil plane holds the clear value 0x5a");
        }
        Cases::add(
            std::move(spec),
            [entry](Rig& rig, CaptureSet& out) {
                TargetDesc td;
                td.depth = entry.format;
                Target& target = rig.make_target(td);
                target.clear_stencil = 0x5a;
                std::copy(kClear, kClear + 4, target.clear_color);
                QuadKit quads(rig);
                PipelineDesc pipe = quads.desc(target);
                pipe.depth_test = true;
                const VkPipeline pipeline = rig.pipe(pipe);
                Image color = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                    quads.quad(cb, -0.9f, -0.9f, 1.8f, 1.8f, palette(0), 0.2f, 0.6f);
                    quads.quad(cb, -0.9f, -0.9f, 1.8f, 1.8f, palette(2), 0.8f, -0.6f);
                    quads.quad(cb, -0.4f, -0.4f, 0.8f, 0.8f, palette(3), 0.5f, 0.0f);
                }, quads.layout);
                const bool has_stencil = entry.format == VK_FORMAT_D16_UNORM_S8_UINT || entry.format == VK_FORMAT_D24_UNORM_S8_UINT || entry.format == VK_FORMAT_D32_SFLOAT_S8_UINT;
                capture_depth_and_color(rig, target, out, std::move(color), true, has_stencil, false);
            },
            [entry](Kit& kit) { return Cases::need_format(kit, entry.format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "depth format"); });
    }
}

class StencilOpScene : public RigScene {
public:
    StencilOpScene(unsigned which, VkStencilOp op) : which_(which), op_(op) {}

protected:
    std::string caps(Kit& kit) override {
        return Cases::need_format(kit, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "D32S8");
    }

    void init(Rig& rig) override {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
        target_ = &rig.make_target(td);
        std::copy(kClear, kClear + 4, target_->clear_color);
        target_->clear_stencil = op_ == VK_STENCIL_OP_INCREMENT_AND_CLAMP || op_ == VK_STENCIL_OP_INCREMENT_AND_WRAP ? 0xfe
                                 : op_ == VK_STENCIL_OP_DECREMENT_AND_CLAMP || op_ == VK_STENCIL_OP_DECREMENT_AND_WRAP ? 0x01
                                                                                                                         : 0x80;
        quads_ = std::make_unique<QuadKit>(rig);
        PipelineDesc occluder = quads_->desc(*target_);
        occluder.depth_test = true;
        PipelineDesc pipe = quads_->desc(*target_);
        pipe.depth_test = true;
        pipe.stencil_test = true;
        pipe.stencil.compareOp = which_ == 1 ? VK_COMPARE_OP_NEVER : VK_COMPARE_OP_ALWAYS;
        pipe.stencil.compareMask = 0xff;
        pipe.stencil.writeMask = 0xff;
        pipe.stencil.reference = 0x7f;
        pipe.stencil.passOp = which_ == 0 ? op_ : VK_STENCIL_OP_KEEP;
        pipe.stencil.failOp = which_ == 1 ? op_ : VK_STENCIL_OP_KEEP;
        pipe.stencil.depthFailOp = which_ == 2 ? op_ : VK_STENCIL_OP_KEEP;
        p_occluder_ = rig.pipe(occluder);
        p_test_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        const float grey[4] = {0.4f, 0.4f, 0.45f, 1.0f};
        const float shift = 0.08f * static_cast<float>(frame);
        rig.run([&](VkCommandBuffer cb) {
            target_->begin(rig.kit, cb, frame == 0);
            rig.bind_graphics(cb, p_occluder_);
            if (frame == 0) {
                quads_->quad(cb, 0.0f, -0.5f, 0.7f, 0.9f, grey, 0.4f);
            }
            rig.bind_graphics(cb, p_test_);
            quads_->quad(cb, -0.9f + shift, -0.9f, 1.2f, 1.0f, palette(0), 0.3f);
            quads_->quad(cb, -0.1f - shift, -0.4f, 1.0f, 1.2f, palette(1), 0.6f);
            quads_->quad(cb, -0.5f, 0.0f + shift, 1.1f, 0.8f, palette(2), 0.5f);
            target_->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override {
        Image stencil;
        std::string error;
        rig.must(target_->read_stencil(rig.kit, stencil, error), error);
        out.add("stencil", std::move(stencil));
    }

private:
    unsigned which_;
    VkStencilOp op_;
    Target* target_ = nullptr;
    std::unique_ptr<QuadKit> quads_;
    VkPipeline p_occluder_ = VK_NULL_HANDLE;
    VkPipeline p_test_ = VK_NULL_HANDLE;
};

void register_stencil_ops() {
    for (unsigned which = 0; which < 3; ++which) {
        const char* kinds[3] = {"pass", "fail", "depth_fail"};
        for (const StencilOpEntry& entry : stencil_ops()) {
            if (entry.op == VK_STENCIL_OP_KEEP) {
                continue;
            }
            for (int multi = 0; multi < 2; ++multi) {
                if (multi != 0 && (which != 0 || (std::string(entry.name) != "increment_clamp" && std::string(entry.name) != "decrement_wrap" && std::string(entry.name) != "invert"))) {
                    continue;
                }
                CaseSpec spec("om", std::string("stencil_") + kinds[which] + "_op_" + entry.name + (multi != 0 ? "_accumulate" : ""));
                spec.tolerance(Tolerance::exact());

                if (multi != 0) {
                    spec.frames(3, {1, 2});
                }
                const StencilOpEntry copy = entry;
                Cases::add_scene(std::move(spec), [which, copy]() -> std::unique_ptr<VkScene> {
                    return std::make_unique<StencilOpScene>(which, copy.op);
                });
            }
        }
    }
}

void register_stencil_compare() {
    for (const CompareEntry& entry : compare_ops()) {
        CaseSpec spec("om", std::string("stencil_compare_") + entry.name);
        spec.tolerance(Tolerance::exact()).uniform_ok("color");
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            target.clear_stencil = 0x40;
            QuadKit quads(rig);
            PipelineDesc write = quads.desc(target);
            write.stencil_test = true;
            write.write_mask = 0;
            write.stencil.compareOp = VK_COMPARE_OP_ALWAYS;
            write.stencil.passOp = VK_STENCIL_OP_REPLACE;
            write.stencil.failOp = VK_STENCIL_OP_KEEP;
            write.stencil.depthFailOp = VK_STENCIL_OP_KEEP;
            write.stencil.compareMask = 0xff;
            write.stencil.writeMask = 0xff;
            write.dynamic = {VK_DYNAMIC_STATE_STENCIL_REFERENCE};
            PipelineDesc test = quads.desc(target);
            test.stencil_test = true;
            test.stencil.compareOp = entry.op;
            test.stencil.passOp = VK_STENCIL_OP_KEEP;
            test.stencil.failOp = VK_STENCIL_OP_KEEP;
            test.stencil.depthFailOp = VK_STENCIL_OP_KEEP;
            test.stencil.compareMask = 0xff;
            test.stencil.writeMask = 0xff;
            test.stencil.reference = 0x40;
            const VkPipeline p_write = rig.pipe(write);
            const VkPipeline p_test = rig.pipe(test);
            const std::uint32_t refs[3] = {0x20, 0x40, 0x60};
            Image color = rig.draw(target, p_write, nullptr, [&](VkCommandBuffer cb) {
                for (unsigned i = 0; i < 3; ++i) {
                    rig.kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, refs[i]);
                    quads.quad(cb, -1.0f + 0.67f * static_cast<float>(i), -1, 0.66f, 1.3f, palette(i));
                }
                rig.bind_graphics(cb, p_test);
                quads.quad(cb, -1, -1, 2, 2, palette(5));
            }, quads.layout);
            capture_depth_and_color(rig, target, out, std::move(color), false, false);
        });
    }
}


void register_misc() {
    for (const char* kind : {"any", "greater", "less", "unchanged"}) {
        CaseSpec spec("om", std::string("frag_depth_") + kind);
        spec.tolerance(Tolerance::exact());
        const std::string shader = std::string("depth_") + kind + ".frag";
        Cases::add(std::move(spec), [shader](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc modified = quads.desc(target);
            modified.depth_test = true;
            modified.fragment = rig.sh(shader.c_str());
            PipelineDesc plain = quads.desc(target);
            plain.depth_test = true;
            const VkPipeline p_modified = rig.pipe(modified);
            const VkPipeline p_plain = rig.pipe(plain);
            Image color = rig.draw(target, p_modified, nullptr, [&](VkCommandBuffer cb) {
                quads.quad(cb, -0.9f, -0.8f, 1.3f, 1.6f, palette(0), 0.5f);
                rig.bind_graphics(cb, p_plain);
                quads.quad(cb, -0.3f, -0.6f, 1.2f, 0.7f, palette(2), 0.5f);
                quads.quad(cb, -0.5f, 0.1f, 1.4f, 0.7f, palette(3), 0.55f);
            }, quads.layout);
            capture_depth_and_color(rig, target, out, std::move(color), true, false);
        });
    }
    CaseSpec exported("om", "stencil_export");
    exported.extension("VK_EXT_shader_stencil_export").tolerance(Tolerance::exact());
    Cases::add(std::move(exported), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.fragment = rig.sh("stencil_export.frag");
        pipe.stencil_test = true;
        pipe.stencil.compareOp = VK_COMPARE_OP_ALWAYS;
        pipe.stencil.passOp = VK_STENCIL_OP_REPLACE;
        pipe.stencil.failOp = VK_STENCIL_OP_KEEP;
        pipe.stencil.depthFailOp = VK_STENCIL_OP_KEEP;
        pipe.stencil.compareMask = 0xff;
        pipe.stencil.writeMask = 0xff;
        const VkPipeline pipeline = rig.pipe(pipe);
        Image color = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) { quads.quad(cb, -0.9f, -0.9f, 1.8f, 1.8f, palette(4), 0.5f); }, quads.layout);
        capture_depth_and_color(rig, target, out, std::move(color), false, true);
    });
    Cases::add(CaseSpec("om", "stencil_front_back_ops").tolerance(Tolerance::exact()), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        target.clear_stencil = 0x20;
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.stencil_test = true;
        pipe.separate_back = true;
        pipe.stencil = VkStencilOpState{VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 0xff, 0xff, 0};
        pipe.stencil_back = VkStencilOpState{VK_STENCIL_OP_KEEP, VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 0xff, 0xff, 0};
        const VkPipeline pipeline = rig.pipe(pipe);
        Image color = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 1.0f, 1.0f, palette(0));
            quads.quad(cb, 0.1f, -0.1f, -1.0f, 1.0f, palette(1));
            quads.quad(cb, -0.2f, 0.2f, 1.0f, 0.7f, palette(2));
            quads.quad(cb, 0.8f, -0.8f, -0.5f, 0.9f, palette(3));
            quads.quad(cb, 0.0f, -0.3f, 0.8f, -0.5f, palette(4));
        }, quads.layout);
        capture_depth_and_color(rig, target, out, std::move(color), false, true);
    });
    CaseSpec bounds("om", "depth_bounds_static");
    bounds.feature("depthBounds").tolerance(Tolerance::exact());
    Cases::add(std::move(bounds), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc fill = quads.desc(target);
        fill.depth_test = true;
        fill.depth_op = VK_COMPARE_OP_ALWAYS;
        PipelineDesc pipe = quads.desc(target);
        pipe.depth_test = true;
        pipe.depth_write = false;
        pipe.depth_op = VK_COMPARE_OP_ALWAYS;
        pipe.depth_bounds = true;
        pipe.bounds_min = 0.3f;
        pipe.bounds_max = 0.7f;
        const VkPipeline p_fill = rig.pipe(fill);
        const VkPipeline p_test = rig.pipe(pipe);
        Image color = rig.draw(target, p_fill, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 8; ++i) {
                quads.quad(cb, -1.0f + 0.25f * static_cast<float>(i), -1, 0.25f, 2, palette(i), 0.05f + 0.13f * static_cast<float>(i));
            }
            rig.bind_graphics(cb, p_test);
            quads.quad(cb, -1, -0.5f, 2, 1.0f, palette(7), 0.5f);
        }, quads.layout);
        capture_depth_and_color(rig, target, out, std::move(color), true, false);
    });
}

const int registered = (register_misc(), register_compare(), register_formats(), register_stencil_ops(), register_stencil_compare(), 0);

}  // namespace

}  // namespace lsw::render::vk
