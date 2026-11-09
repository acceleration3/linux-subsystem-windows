#pragma once

#include "render/common/image.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <utility>

namespace lsw::render {
struct DeviceInfo {
    std::string generator;
    std::string gpu_name;
    std::string vendor_id;
    std::string device_id;
    std::string vulkan_uuid;
    std::string gl_renderer;
    std::string gl_uuid;
    std::string driver_version;
    std::string created;
};

class RefStore {
public:
    explicit RefStore(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] bool exists() const;

    [[nodiscard]] static std::string frame_name(unsigned frame, const std::string& label);
    [[nodiscard]] std::filesystem::path frame_path(const std::string& id, unsigned frame, const std::string& label) const;

    [[nodiscard]] bool write_frame(const std::string& id, unsigned frame, const std::string& label, const Image& image,
                                   std::string& error) const;
    [[nodiscard]] static bool write_image(const std::filesystem::path& path, const Image& image, std::string& error);
    [[nodiscard]] bool read_frame(const std::string& id, unsigned frame, const std::string& label, Image& out,
                                  std::string& error) const;

    [[nodiscard]] bool write_skip(const std::string& id, const std::string& reason, std::string& error) const;
    [[nodiscard]] bool read_skip(const std::string& id, std::string& reason) const;
    void clear_case(const std::string& id) const;

    [[nodiscard]] bool write_recipe(const std::string& id, const std::string& recipe, std::string& error) const;
    [[nodiscard]] bool read_recipe(const std::string& id, std::string& recipe) const;
    [[nodiscard]] std::set<std::pair<unsigned, std::string>> list_frames(const std::string& id) const;

    [[nodiscard]] bool write_device(const DeviceInfo& info, std::string& error) const;
    [[nodiscard]] bool read_device(DeviceInfo& info, std::string& error) const;

private:
    std::filesystem::path root_;
};
}  // namespace lsw::render
