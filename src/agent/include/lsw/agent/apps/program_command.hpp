#pragma once

#include <string>

namespace lsw::agent::apps {

// What a registered command line starts.
//
// The Uninstall key, the services key and a file type's open verb all
// store a command line, and each of them is read for the program in it,
// so the reading lives here once.
class ProgramCommand {
public:
    ProgramCommand() = delete;

    // The program a command line starts: quoted, or up to its ".exe" -
    // uninstallers, services and shell verbs are as often registered
    // unquoted, spaces and all. Empty for an unterminated quote.
    [[nodiscard]] static std::wstring program_of(const std::wstring& command_line);

    // An "open" verb's command line cut where the document goes: the
    // program, and the arguments before the first %1, %L or %* (``-jar``
    // of ``"java.exe" -jar "%1" %*``), which have to come before the
    // document on any command line that opens one. What follows the
    // document is dropped. ``placeholder`` says there was one.
    struct DocumentCommand {
        std::wstring program;
        std::wstring leading_args;
        bool placeholder = false;
    };
    [[nodiscard]] static DocumentCommand split_for_document(const std::wstring& command_line);
};

}  // namespace lsw::agent::apps
