#pragma once

#include <filesystem>
#include <string>

namespace lsw::agent::win {

// A file's SHA-256, in lowercase hex, through CNG.
//
// What the manager compares with the files it ships: the agent's own
// binary, the window mirror's guest half, the driver package. Some of
// those are asked about every two seconds, so each file's digest is kept
// with the size and last write time it was read at, and read again only
// once either changes. A file that is running - the agent itself - is
// read too: it is opened sharing everything.
class FileDigest {
public:
    FileDigest() = delete;

    // Empty when the file is not there or cannot be read.
    [[nodiscard]] static std::string sha256(const std::filesystem::path& path);
};

}  // namespace lsw::agent::win
