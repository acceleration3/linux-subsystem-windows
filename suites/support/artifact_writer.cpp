#include "support/artifact_writer.hpp"

#include "support/png_writer.hpp"

#include <fstream>

namespace lsw::suites {

bool ArtifactWriter::bytes(const std::string& name, const void* data, std::size_t size) {
    const std::filesystem::path path = context_.artifacts_dir() / (name + ".bin");
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file.good()) {
        return false;
    }
    context_.artifact(path);
    return true;
}

bool ArtifactWriter::image(const std::string& name, const void* bgra, std::uint32_t width,
                           std::uint32_t height, std::uint32_t stride) {
    const std::filesystem::path path = context_.artifacts_dir() / (name + ".png");
    if (!PngWriter::write_bgra(path, bgra, width, height, stride)) {
        return false;
    }
    context_.artifact(path);
    return true;
}

}  // namespace lsw::suites
