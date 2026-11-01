#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lsw::agent::apps {

// One Start menu shortcut that is not a packaged application, as the shell
// reports it: what it is called and what it starts.
struct StartMenuEntry {
    std::wstring name;
    // System.Link.TargetParsingPath: a program, a URL, a shell folder
    // ({GUID}), a console snap-in ...
    std::wstring target;
    // System.Link.Arguments.
    std::wstring arguments;
};

// Which Start menu shortcuts are applications, and which of several for
// one program is the application.
//
// Platform-free, so it is tested on the build machine.
class StartMenuFilter {
public:
    StartMenuFilter() = delete;

    // The entries that are applications: the target is an existing .exe
    // (``exists`` decides), is not under ``windows_dir`` - Windows' own
    // tools are not what a person installed - and is not an uninstaller.
    // Then one entry per program: several shortcuts to one .exe are
    // different ways to start it, and the one with no arguments is the
    // program ("OneNote" absorbs "Sticky Notes" for ONENOTE.EXE
    // /memoryWindow). A program with no such shortcut keeps each one (with
    // its arguments): they are different applications, as Chrome's web
    // apps are chrome_proxy.exe with an --app-id each. Order is that of the
    // first shortcut seen for each program.
    [[nodiscard]] static std::vector<StartMenuEntry>
    applications(const std::vector<StartMenuEntry>& entries, const std::wstring& windows_dir,
                 const std::function<bool(const std::wstring&)>& exists);

    [[nodiscard]] static bool is_uninstaller(const std::wstring& exe);
};

}  // namespace lsw::agent::apps
