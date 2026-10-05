#include "lsw/agent/apps/launch_command.hpp"

#include "lsw/agent/util/wide_string.hpp"

namespace lsw::agent::apps {

LaunchCommand::LaunchCommand(const std::wstring& path, const std::wstring& windows_dir,
                             const std::wstring& args)
    : application(path) {
    const auto quoted = [](const std::wstring& value) { return L"\"" + value + L"\""; };
    std::wstring prefix;
    bool batch = false;
    if (util::WideString::iends_with(path, L".msi") ||
        util::WideString::iends_with(path, L".msp")) {
        application = windows_dir + L"\\System32\\msiexec.exe";
        prefix = util::WideString::iends_with(path, L".msi") ? L" /i " : L" /p ";
    } else if (util::WideString::iends_with(path, L".bat") ||
               util::WideString::iends_with(path, L".cmd")) {
        application = windows_dir + L"\\System32\\cmd.exe";
        prefix = L" /d /s /c \"";
        batch = true;
    } else if (util::WideString::iends_with(path, L".ps1")) {
        application = windows_dir + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        prefix = L" -NoLogo -NoProfile -ExecutionPolicy Bypass -File ";
    } else if (util::WideString::iends_with(path, L".vbs") ||
               util::WideString::iends_with(path, L".js") ||
               util::WideString::iends_with(path, L".wsf")) {
        application = windows_dir + L"\\System32\\wscript.exe";
        prefix = L" ";
    }
    command_line = quoted(application);
    if (!prefix.empty())
        command_line += prefix + quoted(path);
    if (!args.empty())
        command_line += L" " + args;
    if (batch)
        command_line += L"\"";
}

}  // namespace lsw::agent::apps
