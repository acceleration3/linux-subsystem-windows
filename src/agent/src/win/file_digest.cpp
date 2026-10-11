#include "lsw/agent/win/file_digest.hpp"

#include "lsw/agent/win/unique_handle.hpp"

// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace lsw::agent::win {

namespace {

struct Seen {
    std::uint64_t size = 0;
    std::uint64_t written = 0;
    std::string digest;
};

std::mutex cache_mutex;
std::map<std::wstring, Seen> cache;

std::uint64_t join(DWORD high, DWORD low) {
    return (std::uint64_t{high} << 32U) | low;
}

// The digest of what ``file`` holds from where it is now to its end, or
// empty when a read or CNG fails.
std::string digest_of(HANDLE file) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return {};
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string out;
    if (BCRYPT_SUCCESS(::BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<UCHAR> chunk(1U << 20U);
        bool read_all = false;
        for (;;) {
            DWORD got = 0;
            if (::ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr) ==
                FALSE) {
                break;
            }
            if (got == 0) {
                read_all = true;
                break;
            }
            if (!BCRYPT_SUCCESS(::BCryptHashData(hash, chunk.data(), got, 0))) {
                break;
            }
        }
        std::array<UCHAR, 32> value{};
        if (read_all && BCRYPT_SUCCESS(::BCryptFinishHash(hash, value.data(),
                                                          static_cast<ULONG>(value.size()), 0))) {
            constexpr char kHex[] = "0123456789abcdef";
            for (const UCHAR byte : value) {
                out.push_back(kHex[byte >> 4U]);
                out.push_back(kHex[byte & 0x0fU]);
            }
        }
        ::BCryptDestroyHash(hash);
    }
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    return out;
}

}  // namespace

std::string FileDigest::sha256(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) == FALSE ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return {};
    }
    const std::uint64_t size = join(attributes.nFileSizeHigh, attributes.nFileSizeLow);
    const std::uint64_t written =
        join(attributes.ftLastWriteTime.dwHighDateTime, attributes.ftLastWriteTime.dwLowDateTime);
    {
        const std::lock_guard lock{cache_mutex};
        const auto found = cache.find(path.wstring());
        if (found != cache.end() && found->second.size == size &&
            found->second.written == written) {
            return found->second.digest;
        }
    }
    UniqueHandle file{::CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file) {
        return {};
    }
    std::string digest = digest_of(file.get());
    if (!digest.empty()) {
        const std::lock_guard lock{cache_mutex};
        cache[path.wstring()] = Seen{size, written, digest};
    }
    return digest;
}

}  // namespace lsw::agent::win
