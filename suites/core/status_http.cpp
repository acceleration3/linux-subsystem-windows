#include "core/status_http.hpp"

#include "core/text.hpp"

#include <windows.h>
#include <winhttp.h>

namespace lsw::suites {

namespace {

constexpr int kResolveMs = 300;
constexpr int kConnectMs = 500;
constexpr int kSendMs = 500;
constexpr int kReceiveMs = 1000;

}  // namespace

StatusHttp::StatusHttp(const std::string& url) {
    const std::wstring wide = Text::wide(url);
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 1024;
    if (::WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) != FALSE) {
        host_ = host;
        path_ = path[0] == L'\0' ? L"/" : path;
        port_ = parts.nPort;
        secure_ = parts.nScheme == INTERNET_SCHEME_HTTPS;
    }
}

StatusHttp::~StatusHttp() {
    disconnect();
    if (session_ != nullptr) {
        ::WinHttpCloseHandle(session_);
    }
}

void StatusHttp::disconnect() {
    if (connection_ != nullptr) {
        ::WinHttpCloseHandle(connection_);
        connection_ = nullptr;
    }
}

int StatusHttp::post(const Json& batch) {
    if (host_.empty()) {
        return 0;
    }
    if (session_ == nullptr) {
        session_ = ::WinHttpOpen(L"lsw-suites", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_ == nullptr) {
            return 0;
        }
        ::WinHttpSetTimeouts(session_, kResolveMs, kConnectMs, kSendMs, kReceiveMs);
    }
    if (connection_ == nullptr) {
        connection_ = ::WinHttpConnect(session_, host_.c_str(), port_, 0);
        if (connection_ == nullptr) {
            return 0;
        }
    }
    const HINTERNET request =
        ::WinHttpOpenRequest(connection_, L"POST", path_.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0);
    if (request == nullptr) {
        disconnect();
        return 0;
    }
    std::string body = batch.dump(0);
    const DWORD size = static_cast<DWORD>(body.size());
    DWORD code = 0;
    DWORD code_size = sizeof(code);
    const bool answered =
        ::WinHttpSendRequest(request, L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
                             body.data(), size, size, 0) != FALSE &&
        ::WinHttpReceiveResponse(request, nullptr) != FALSE &&
        ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size,
                              WINHTTP_NO_HEADER_INDEX) != FALSE;
    if (answered) {
        char sink[256];
        DWORD got = 0;
        while (::WinHttpReadData(request, sink, sizeof(sink), &got) != FALSE && got > 0) {
        }
    }
    ::WinHttpCloseHandle(request);
    if (!answered) {
        disconnect();
        return 0;
    }
    return static_cast<int>(code);
}

}  // namespace lsw::suites
