#pragma once

#include <string>

namespace lsw::agent::apps {
class ProgramCommand {
public:
    ProgramCommand() = delete;

    [[nodiscard]] static std::wstring program_of(const std::wstring& command_line);

    struct DocumentCommand {
        std::wstring program;
        std::wstring leading_args;
        bool placeholder = false;
    };
    [[nodiscard]] static DocumentCommand split_for_document(const std::wstring& command_line);
};

}
