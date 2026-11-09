#pragma once

#include "render/common/case.hpp"
#include "render/common/diff.hpp"
#include "render/common/image.hpp"
#include "render/common/tolerance.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lsw::render {
struct CaptureRecord {
    unsigned frame = 0;
    std::string label;
    const Image* actual = nullptr;
    const Image* oracle = nullptr;
    Tolerance tolerance;
    bool judged = false;
    DiffResult result;
};

class CaptureDump {
public:
    [[nodiscard]] static bool enabled();

    [[nodiscard]] static std::string hash(const Image& image);

    [[nodiscard]] static Image preview(const Image& image);

    [[nodiscard]] static std::string file_label(unsigned frame, const std::string& label);

    [[nodiscard]] static std::vector<std::filesystem::path> write(const std::filesystem::path& dir, const std::string& id,
                                                                  const CaseSpec& spec, const std::vector<CaptureRecord>& records,
                                                                  const std::string& side, const std::string& outcome, bool write_images);

private:
    [[nodiscard]] static std::string stats(const Image& image, const Image* previous);
    [[nodiscard]] static std::string escape(const std::string& text);
    [[nodiscard]] static std::string number(double value);
};
}  // namespace lsw::render
