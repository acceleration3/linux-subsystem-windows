#include "lsw/agent/apps/exit_status.hpp"

#include <cstdio>

namespace lsw::agent::apps {

bool ExitStatus::is_load_failure(std::uint32_t code) {
    return code == kDllNotFound || code == kEntryPointNotFound || code == kInvalidImageFormat ||
           code == kDllInitFailed || is_side_by_side(code);
}

std::string ExitStatus::name(std::uint32_t code) {
    switch (code) {
    case kDllNotFound:
        return "DLL not found";
    case kEntryPointNotFound:
        return "entry point not found";
    case kInvalidImageFormat:
        return "bad image format";
    case kDllInitFailed:
        return "DLL initialization failed";
    case kAccessViolation:
        return "access violation";
    case kStackOverflow:
        return "stack overflow";
    case kFastFail:
        return "fast fail (stack buffer overrun or abort)";
    case kHeapCorruption:
        return "heap corruption";
    case kIntegerDivideByZero:
        return "integer divide by zero";
    case kIllegalInstruction:
        return "illegal instruction";
    case kPrivilegedInstruction:
        return "privileged instruction";
    case kInPageError:
        return "in-page error";
    case kInvalidHandle:
        return "invalid handle";
    case kNoMemory:
        return "out of memory";
    default:
        break;
    }
    return is_side_by_side(code) ? "side-by-side configuration error" : "unrecognized error";
}

std::string ExitStatus::hex(std::uint32_t code) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", code);
    return buf;
}

}
