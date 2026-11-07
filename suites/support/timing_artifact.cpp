#include "support/timing_artifact.hpp"

#include <fstream>

namespace lsw::suites {

TimingArtifact::TimingArtifact(Context& context, std::string name, const std::string& header) : context_(context), name_(std::move(name)), content_(header + "\n") {}

void TimingArtifact::row(const std::vector<std::string>& cells) {
    for (std::size_t i = 0; i < cells.size(); ++i) {
        content_ += (i == 0 ? "" : ",") + cells[i];
    }
    content_ += "\n";
}

bool TimingArtifact::write() {
    return text(context_, name_ + ".csv", content_);
}

bool TimingArtifact::text(Context& context, const std::string& file_name, const std::string& content) {
    const std::filesystem::path path = context.artifacts_dir() / file_name;
    std::ofstream file(path, std::ios::binary);
    file << content;
    if (!file.good()) {
        return false;
    }
    context.artifact(path);
    return true;
}

}  // namespace lsw::suites
