#include "render/common/ref_store.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace lsw::render {

namespace {

constexpr std::uint32_t kMagic = 0x5257534cu;
constexpr std::uint32_t kVersion = 1;

void put32(std::uint8_t* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint32_t get32(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c == '\n' ? ' ' : c);
    }
    return out;
}

std::string field(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    std::size_t at = json.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    at = json.find('"', json.find(':', at));
    if (at == std::string::npos) {
        return {};
    }
    std::string out;
    for (++at; at < json.size() && json[at] != '"'; ++at) {
        if (json[at] == '\\' && at + 1 < json.size()) {
            ++at;
        }
        out.push_back(json[at]);
    }
    return out;
}

}  // namespace

bool RefStore::exists() const {
    std::error_code ignored;
    return std::filesystem::is_directory(root_, ignored);
}

std::string RefStore::frame_name(unsigned frame, const std::string& label) {
    char text[32];
    std::snprintf(text, sizeof text, "f%03u", frame);
    std::string name = text;
    if (!label.empty()) {
        name += "_" + label;
    }
    return name + ".rref";
}

std::filesystem::path RefStore::frame_path(const std::string& id, unsigned frame, const std::string& label) const {
    return root_ / id / frame_name(frame, label);
}

bool RefStore::write_frame(const std::string& id, unsigned frame, const std::string& label, const Image& image,
                           std::string& error) const {
    const std::filesystem::path path = frame_path(id, frame, label);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot write " + path.string();
        return false;
    }
    std::uint8_t header[32] = {};
    put32(header + 0, kMagic);
    put32(header + 4, kVersion);
    put32(header + 8, image.width());
    put32(header + 12, image.height());
    put32(header + 16, static_cast<std::uint32_t>(image.kind()));
    const std::uint64_t payload = image.bytes().size();
    put32(header + 24, static_cast<std::uint32_t>(payload));
    put32(header + 28, static_cast<std::uint32_t>(payload >> 32));
    file.write(reinterpret_cast<const char*>(header), sizeof header);
    file.write(reinterpret_cast<const char*>(image.bytes().data()), static_cast<std::streamsize>(image.bytes().size()));
    if (!file) {
        error = "short write to " + path.string();
        return false;
    }
    return true;
}

bool RefStore::read_frame(const std::string& id, unsigned frame, const std::string& label, Image& out,
                          std::string& error) const {
    const std::filesystem::path path = frame_path(id, frame, label);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "missing reference " + path.string();
        return false;
    }
    std::uint8_t header[32];
    file.read(reinterpret_cast<char*>(header), sizeof header);
    if (!file || get32(header) != kMagic || get32(header + 4) != kVersion) {
        error = "bad reference header in " + path.string();
        return false;
    }
    const std::uint32_t width = get32(header + 8);
    const std::uint32_t height = get32(header + 12);
    const std::uint32_t kind = get32(header + 16);
    const std::uint64_t payload = static_cast<std::uint64_t>(get32(header + 24)) | (static_cast<std::uint64_t>(get32(header + 28)) << 32);
    if (kind > static_cast<std::uint32_t>(PixelKind::R8Uint) || width == 0 || height == 0 || width > 16384 || height > 16384) {
        error = "bad reference dimensions in " + path.string();
        return false;
    }
    Image image(width, height, static_cast<PixelKind>(kind));
    if (image.bytes().size() != payload) {
        error = "reference payload size mismatch in " + path.string();
        return false;
    }
    file.read(reinterpret_cast<char*>(image.bytes().data()), static_cast<std::streamsize>(payload));
    if (!file) {
        error = "truncated reference " + path.string();
        return false;
    }
    out = std::move(image);
    return true;
}

bool RefStore::write_skip(const std::string& id, const std::string& reason, std::string& error) const {
    const std::filesystem::path dir = root_ / id;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream file(dir / "skip.txt", std::ios::trunc);
    if (!file) {
        error = "cannot write skip marker in " + dir.string();
        return false;
    }
    file << reason;
    return static_cast<bool>(file);
}

bool RefStore::read_skip(const std::string& id, std::string& reason) const {
    std::ifstream file(root_ / id / "skip.txt");
    if (!file) {
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    reason = text.str();
    return true;
}

void RefStore::clear_case(const std::string& id) const {
    std::error_code ec;
    std::filesystem::remove_all(root_ / id, ec);
}

bool RefStore::write_device(const DeviceInfo& info, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    std::ofstream file(root_ / "device.json", std::ios::trunc);
    if (!file) {
        error = "cannot write device.json in " + root_.string();
        return false;
    }
    file << "{\n"
         << "  \"generator\": \"" << escape(info.generator) << "\",\n"
         << "  \"gpu_name\": \"" << escape(info.gpu_name) << "\",\n"
         << "  \"vendor_id\": \"" << escape(info.vendor_id) << "\",\n"
         << "  \"device_id\": \"" << escape(info.device_id) << "\",\n"
         << "  \"vulkan_uuid\": \"" << escape(info.vulkan_uuid) << "\",\n"
         << "  \"gl_renderer\": \"" << escape(info.gl_renderer) << "\",\n"
         << "  \"gl_uuid\": \"" << escape(info.gl_uuid) << "\",\n"
         << "  \"driver_version\": \"" << escape(info.driver_version) << "\",\n"
         << "  \"created\": \"" << escape(info.created) << "\"\n"
         << "}\n";
    return static_cast<bool>(file);
}

bool RefStore::read_device(DeviceInfo& info, std::string& error) const {
    std::ifstream file(root_ / "device.json");
    if (!file) {
        error = "no device.json in " + root_.string();
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    const std::string json = text.str();
    info.generator = field(json, "generator");
    info.gpu_name = field(json, "gpu_name");
    info.vendor_id = field(json, "vendor_id");
    info.device_id = field(json, "device_id");
    info.vulkan_uuid = field(json, "vulkan_uuid");
    info.gl_renderer = field(json, "gl_renderer");
    info.gl_uuid = field(json, "gl_uuid");
    info.driver_version = field(json, "driver_version");
    info.created = field(json, "created");
    return true;
}

}  // namespace lsw::render
