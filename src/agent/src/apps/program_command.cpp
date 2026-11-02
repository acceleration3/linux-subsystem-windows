#include "lsw/agent/apps/program_command.hpp"

#include "lsw/agent/util/wide_string.hpp"

#include <vector>

namespace lsw::agent::apps {

std::wstring ProgramCommand::program_of(const std::wstring& command_line) {
    if (!command_line.empty() && command_line.front() == L'"') {
        const auto end = command_line.find(L'"', 1);
        return end == std::wstring::npos ? std::wstring{} : command_line.substr(1, end - 1);
    }
    const auto exe = util::WideString::lower(command_line).find(L".exe");
    return exe == std::wstring::npos ? command_line : command_line.substr(0, exe + 4);
}

ProgramCommand::DocumentCommand ProgramCommand::split_for_document(const std::wstring& command_line) {
    DocumentCommand out;
    out.program = program_of(command_line);
    if (out.program.empty()) {
        return out;
    }
    std::size_t at = out.program.size();
    if (!command_line.empty() && command_line.front() == L'"') {
        at += 2;
    }
    const std::wstring rest = at <= command_line.size() ? command_line.substr(at) : std::wstring{};

    std::vector<std::wstring> words;
    std::wstring word;
    bool quoted = false;
    for (const wchar_t c : rest) {
        if (c == L'"') {
            quoted = !quoted;
            word += c;
        } else if ((c == L' ' || c == L'\t') && !quoted) {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    for (const std::wstring& w : words) {
        if (w.find(L"%1") != std::wstring::npos || w.find(L"%L") != std::wstring::npos ||
            w.find(L"%l") != std::wstring::npos || w.find(L"%*") != std::wstring::npos) {
            out.placeholder = true;
            break;
        }
        if (!out.leading_args.empty()) {
            out.leading_args += L' ';
        }
        out.leading_args += w;
    }
    return out;
}

}
