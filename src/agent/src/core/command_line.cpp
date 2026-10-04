#include "lsw/agent/core/command_line.hpp"

namespace lsw::agent::core {

CommandLine CommandLine::parse(const std::vector<std::wstring>& args) {
    CommandLine out;
    bool saw_mode = false;
    const auto set_mode = [&](Mode mode, const std::wstring& flag) {
        if (saw_mode) {
            out.error = L"more than one mode given (" + flag + L")";
            return;
        }
        saw_mode = true;
        out.mode = mode;
    };

    for (std::size_t i = 0; i < args.size() && out.error.empty(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"--service") {
            set_mode(Mode::Service, a);
        } else if (a == L"--session") {
            set_mode(Mode::SessionHelper, a);
        } else if (a == L"--pipe") {
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                out.error = L"--pipe needs a pipe name";
                break;
            }
            out.pipe = args[++i];
        } else {
            out.error = L"unknown argument: " + a;
        }
    }
    if (out.error.empty() && out.mode == Mode::SessionHelper && out.pipe.empty()) {
        out.error = L"--session needs --pipe";
    }
    if (out.error.empty() && out.mode != Mode::SessionHelper && !out.pipe.empty()) {
        out.error = L"--pipe is only for --session";
    }
    return out;
}

}  // namespace lsw::agent::core
