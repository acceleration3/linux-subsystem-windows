#pragma once

#include <string>

namespace lsw::agent::win {

// Win32 and COM failure codes, spelled for a log line.
//
// Every failure path in the agent reports one of these, and each used to
// format it by hand: some in decimal after a "0x" prefix, which reads as
// hex and is not, and none with the system's own text — so the reader of
// a log had to look ``1307`` up before learning what had gone wrong.
class ErrorText {
public:
    ErrorText() = delete;

    // ``0x80070005``. HRESULTs are bit fields — severity, facility,
    // code — and only read as such in hex. ``hr`` is an HRESULT, typed
    // as long so this header stays free of <windows.h>.
    [[nodiscard]] static std::string hresult(long hr);

    // ``5 (Access is denied.)``: the number first, so a grep for the
    // code still finds it, then FormatMessage's text when it has any.
    [[nodiscard]] static std::string win32(unsigned long code);

    // ``win32(GetLastError())``. Call it before anything else that could
    // overwrite the thread's last error — including the other arguments
    // of the same log call, whose evaluation order is unspecified.
    [[nodiscard]] static std::string last_error();
};

}  // namespace lsw::agent::win
