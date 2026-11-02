#include "lsw/agent/util/powershell_command.hpp"

#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <cstdint>
#include <vector>

namespace lsw::agent::util {

std::wstring PowerShellCommand::encoded(const std::string& script_utf8) {
    const std::wstring script = TextEncoding::to_wide(script_utf8);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(script.size() * 2);
    for (const wchar_t c : script) {
        bytes.push_back(static_cast<std::uint8_t>(c & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    }
    return L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden "
           L"-EncodedCommand " +
           TextEncoding::to_wide(Base64::encode(bytes));
}

}
