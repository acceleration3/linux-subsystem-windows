#include "support/handle_types.hpp"

#include <windows.h>
#include <winternl.h>

#include <vector>

namespace lsw::suites {

namespace {

struct HandleEntry {
    USHORT process;
    USHORT creator;
    UCHAR type;
    UCHAR flags;
    USHORT value;
    PVOID object;
    ACCESS_MASK access;
};

struct HandleTable {
    ULONG count;
    HandleEntry entries[1];
};

using QuerySystem = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using QueryObject = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

constexpr ULONG kSystemHandleInformation = 16;
constexpr ULONG kObjectTypeInformation = 2;

}  // namespace

std::map<std::string, long long> HandleTypes::count() {
    std::map<std::string, long long> counts;
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    auto* system = reinterpret_cast<QuerySystem>(reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtQuerySystemInformation")));
    auto* object = reinterpret_cast<QueryObject>(reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtQueryObject")));
    if (system == nullptr || object == nullptr) {
        return counts;
    }
    std::vector<char> table(1u << 22);
    ULONG needed = 0;
    LONG status = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        status = system(kSystemHandleInformation, table.data(), static_cast<ULONG>(table.size()), &needed);
        if (status >= 0) {
            break;
        }
        table.resize(std::max<std::size_t>(table.size() * 2, needed));
    }
    if (status < 0) {
        return counts;
    }
    const auto* handles = reinterpret_cast<const HandleTable*>(table.data());
    const USHORT self = static_cast<USHORT>(::GetCurrentProcessId());
    std::map<UCHAR, std::string> names;
    for (ULONG i = 0; i < handles->count; ++i) {
        const HandleEntry& entry = handles->entries[i];
        if (entry.process != self) {
            continue;
        }
        auto found = names.find(entry.type);
        if (found == names.end()) {
            std::vector<char> info(1024);
            ULONG length = 0;
            std::string name = "type" + std::to_string(entry.type);
            if (object(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(entry.value)), kObjectTypeInformation, info.data(),
                       static_cast<ULONG>(info.size()), &length) >= 0) {
                const auto* type = reinterpret_cast<const UNICODE_STRING*>(info.data());
                name.assign(type->Length / 2, '?');
                for (USHORT c = 0; c < type->Length / 2; ++c) {
                    name[c] = static_cast<char>(type->Buffer[c]);
                }
            }
            found = names.emplace(entry.type, name).first;
        }
        std::string key = found->second;
        if (key == "Section") {
            std::vector<char> info(2048);
            ULONG length = 0;
            if (object(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(entry.value)), 1, info.data(), static_cast<ULONG>(info.size()), &length) >= 0) {
                const auto* name = reinterpret_cast<const UNICODE_STRING*>(info.data());
                key += ":";
                for (USHORT c = 0; c < name->Length / 2; ++c) {
                    key += static_cast<char>(name->Buffer[c]);
                }
                key += " access=" + std::to_string(entry.access);
            }
        }
        ++counts[key];
    }
    return counts;
}

std::string HandleTypes::grown(const std::map<std::string, long long>& before, const std::map<std::string, long long>& after) {
    std::string text;
    for (const auto& item : after) {
        const auto old = before.find(item.first);
        const long long delta = item.second - (old == before.end() ? 0 : old->second);
        if (delta != 0) {
            text += item.first + "+" + std::to_string(delta) + " ";
        }
    }
    return text.empty() ? "none" : text;
}

}  // namespace lsw::suites
