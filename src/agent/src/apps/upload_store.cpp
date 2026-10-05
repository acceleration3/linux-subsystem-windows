#include "lsw/agent/apps/upload_store.hpp"

#include "lsw/linkproto/file_transfer.hpp"

#include <random>
#include <stdexcept>
#include <utility>

namespace lsw::agent::apps {

namespace {

void validate_name(const std::string& name) {
    // No path traversal, alternate streams, device names or cmd expansion.
    if (name.empty() || name.size() > 240 || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("/\\:<>\"|?*%!") != std::string::npos)
        throw std::invalid_argument("not a safe Windows filename");
    for (const char raw : name) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 32 || c == 127)
            throw std::invalid_argument("control character in filename");
    }
    std::string stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ')
        stem.pop_back();
    for (char& c : stem) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" ||
        stem == "CONOUT$" ||
        (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
         stem[3] >= '1' && stem[3] <= '9'))
        throw std::invalid_argument("reserved Windows filename");
}

std::string new_id() {
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 16; ++i) {
        const auto byte = random() & 255U;
        id += hex[byte >> 4];
        id += hex[byte & 15];
    }
    return id;
}

}  // namespace

UploadStore::UploadStore(std::filesystem::path root) : root_(std::move(root)) {}

UploadStore::~UploadStore() {
    for (auto& [id, upload] : uploads_)
        discard(upload);
}

void UploadStore::discard(Upload& upload) noexcept {
    upload.stream.close();
    std::error_code error;
    std::filesystem::remove(upload.directory / "upload.part", error);
    std::filesystem::remove(upload.directory, error);
}

void UploadStore::reap() {
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(5);
    for (auto it = uploads_.begin(); it != uploads_.end();) {
        if (it->second.touched < cutoff) {
            discard(it->second);
            it = uploads_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string UploadStore::begin(const std::string& name, std::uint64_t size) {
    validate_name(name);
    if (size == 0 || size > linkproto::kMaxUploadBytes)
        throw std::invalid_argument("file must be between 1 byte and 16 GiB");
    std::lock_guard lock(mutex_);
    reap();
    if (uploads_.size() >= 8)
        throw std::runtime_error("too many active file transfers");
    std::filesystem::create_directories(root_);
    Upload upload;
    std::string id;
    do {
        id = new_id();
        upload.directory = root_ / id;
    } while (!std::filesystem::create_directory(upload.directory));
    const std::u8string utf8(reinterpret_cast<const char8_t*>(name.data()), name.size());
    upload.destination = upload.directory / std::filesystem::path(utf8);
    upload.stream.open(upload.directory / "upload.part", std::ios::binary | std::ios::trunc);
    if (!upload.stream) {
        discard(upload);
        throw std::runtime_error("cannot create guest upload file");
    }
    upload.size = size;
    upload.touched = std::chrono::steady_clock::now();
    uploads_.emplace(id, std::move(upload));
    return id;
}

std::uint64_t UploadStore::append(const std::string& id, std::uint64_t offset,
                                  std::span<const std::uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    reap();
    auto& upload = uploads_.at(id);
    if (offset != upload.offset || bytes.empty() || bytes.size() > linkproto::kFileChunkBytes ||
        bytes.size() > upload.size - upload.offset)
        throw std::invalid_argument("invalid upload offset or chunk size");
    upload.stream.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
    if (!upload.stream)
        throw std::runtime_error("cannot write guest upload file (disk full?)");
    upload.offset += bytes.size();
    upload.touched = std::chrono::steady_clock::now();
    return upload.offset;
}

std::filesystem::path UploadStore::finish(const std::string& id) {
    std::lock_guard lock(mutex_);
    reap();
    auto& upload = uploads_.at(id);
    if (upload.offset != upload.size)
        throw std::runtime_error("upload is incomplete");
    upload.stream.close();
    if (!upload.stream)
        throw std::runtime_error("cannot flush guest upload file");
    std::filesystem::rename(upload.directory / "upload.part", upload.destination);
    auto path = upload.destination;
    uploads_.erase(id);
    return path;
}

void UploadStore::abort(const std::string& id) {
    std::lock_guard lock(mutex_);
    if (const auto it = uploads_.find(id); it != uploads_.end()) {
        discard(it->second);
        uploads_.erase(it);
    }
}

}  // namespace lsw::agent::apps
