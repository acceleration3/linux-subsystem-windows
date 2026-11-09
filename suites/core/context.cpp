#include "core/context.hpp"

#include "core/child_protocol.hpp"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace lsw::suites {

Context::Context(std::string id, std::filesystem::path artifacts_root)
    : id_(std::move(id)), root_(std::move(artifacts_root)) {}

std::filesystem::path Context::artifacts_dir() {
    std::filesystem::path dir = root_ / id_;
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    return dir;
}

void Context::log(const std::string& line) {
    std::fprintf(stdout, "log: %s\n", line.c_str());
    std::fflush(stdout);
    if (log_path_.empty()) {
        log_path_ = artifacts_dir() / "log.txt";
    }
    std::ofstream file(log_path_, std::ios::app);
    file << line << '\n';
}

void Context::note(const std::string& line) {
    notes_.push_back(line);
}

void Context::phase(const std::string& text) {
    ChildProtocol::phase(text);
}

void Context::measure(const std::string& key, Json value) {
    std::string clean = key;
    for (char& c : clean) {
        c = c == ' ' || c == '\t' ? '_' : c;
    }
    measures_.set(clean, std::move(value));
}

void Context::artifact(const std::filesystem::path& path) {
    artifacts_.push_back(path.string());
}

}  // namespace lsw::suites
