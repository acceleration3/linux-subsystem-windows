#include "core/crash_dump.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace lsw::suites {

namespace {

using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

wchar_t g_path[MAX_PATH * 2];

LONG WINAPI write_dump(EXCEPTION_POINTERS* pointers) {
    HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll");
    auto* write = dbghelp != nullptr ? reinterpret_cast<WriteDump>(reinterpret_cast<void*>(::GetProcAddress(dbghelp, "MiniDumpWriteDump"))) : nullptr;
    if (write != nullptr) {
        HANDLE file = ::CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{};
            exception.ThreadId = ::GetCurrentThreadId();
            exception.ExceptionPointers = pointers;
            exception.ClientPointers = FALSE;
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo | MiniDumpWithHandleData);
            write(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type, &exception, nullptr, nullptr);
            ::CloseHandle(file);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

bool CrashDump::requested() {
    const char* env = std::getenv("LSW_SUITES_KEEP_DUMPS");
    return env != nullptr && *env != '\0' && *env != '0';
}

void CrashDump::install(const std::filesystem::path& file) {
    const std::wstring path = file.wstring();
    if (path.size() >= sizeof g_path / sizeof g_path[0]) {
        return;
    }
    std::memcpy(g_path, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    ::SetUnhandledExceptionFilter(write_dump);
}

}  // namespace lsw::suites
