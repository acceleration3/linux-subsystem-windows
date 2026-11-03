#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::apps {
class ExitStatus {
public:
    ExitStatus() = delete;

    static constexpr std::uint32_t kDllNotFound = 0xC0000135;
    static constexpr std::uint32_t kEntryPointNotFound = 0xC0000139;
    static constexpr std::uint32_t kInvalidImageFormat = 0xC000007B;
    static constexpr std::uint32_t kDllInitFailed = 0xC0000142;
    static constexpr std::uint32_t kAccessViolation = 0xC0000005;
    static constexpr std::uint32_t kStackOverflow = 0xC00000FD;
    static constexpr std::uint32_t kFastFail = 0xC0000409;
    static constexpr std::uint32_t kHeapCorruption = 0xC0000374;
    static constexpr std::uint32_t kIntegerDivideByZero = 0xC0000094;
    static constexpr std::uint32_t kIllegalInstruction = 0xC000001D;
    static constexpr std::uint32_t kPrivilegedInstruction = 0xC0000096;
    static constexpr std::uint32_t kInPageError = 0xC0000006;
    static constexpr std::uint32_t kInvalidHandle = 0xC0000008;
    static constexpr std::uint32_t kNoMemory = 0xC0000017;
    static constexpr std::uint32_t kSideBySideMask = 0xFFFF0000;
    static constexpr std::uint32_t kSideBySidePrefix = 0xC0150000;
    static constexpr std::uint32_t kSeverityMask = 0xE0000000;
    static constexpr std::uint32_t kSeverityError = 0xC0000000;

    [[nodiscard]] static bool is_error(std::uint32_t code) {
        return (code & kSeverityMask) == kSeverityError;
    }

    [[nodiscard]] static bool is_side_by_side(std::uint32_t code) {
        return (code & kSideBySideMask) == kSideBySidePrefix;
    }

    [[nodiscard]] static bool is_load_failure(std::uint32_t code);

    [[nodiscard]] static std::string name(std::uint32_t code);

    [[nodiscard]] static std::string hex(std::uint32_t code);
};

}
