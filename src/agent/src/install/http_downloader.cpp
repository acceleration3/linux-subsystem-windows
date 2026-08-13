#include "lsw/agent/install/http_downloader.hpp"

#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
#include <winhttp.h>
// clang-format on

#include <iterator>
#include <vector>

namespace lsw::agent::install {

namespace {

// Closes the WinHTTP handle trio in the right order however we leave.
class WinHttpHandles {
public:
    ~WinHttpHandles() {
        if (request != nullptr) ::WinHttpCloseHandle(request);
        if (connection != nullptr) ::WinHttpCloseHandle(connection);
        if (session != nullptr) ::WinHttpCloseHandle(session);
    }
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
};

constexpr unsigned long long kProgressIntervalMs = 100;  // ~10 updates/sec

}  // namespace

bool HttpDownloader::fetch(const std::wstring& url, const std::wstring& dest_path,
                           const ProgressCallback& on_progress, std::string& error_out) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = std::size(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = std::size(path);
    if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error_out = "WinHttpCrackUrl failed";
        return false;
    }

    WinHttpHandles h;
    h.session = ::WinHttpOpen(L"lsw-agent/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (h.session == nullptr) {
        error_out = "WinHttpOpen failed err=" + std::to_string(::GetLastError());
        return false;
    }
    h.connection = ::WinHttpConnect(h.session, host, uc.nPort, 0);
    if (h.connection == nullptr) {
        error_out = "WinHttpConnect failed err=" + std::to_string(::GetLastError());
        return false;
    }
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    h.request = ::WinHttpOpenRequest(h.connection, L"GET", path, nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     flags);
    if (h.request == nullptr) {
        error_out = "WinHttpOpenRequest failed err=" + std::to_string(::GetLastError());
        return false;
    }
    if (!::WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !::WinHttpReceiveResponse(h.request, nullptr)) {
        error_out = "WinHttpSendRequest/ReceiveResponse failed err="
                    + std::to_string(::GetLastError());
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    ::WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          nullptr, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        error_out = "HTTP status " + std::to_string(status);
        return false;
    }

    // Content-Length is optional; many CDNs use chunked encoding.
    std::uint64_t total = 0;
    {
        wchar_t len_buf[64] = {};
        DWORD len_size = sizeof(len_buf);
        if (::WinHttpQueryHeaders(h.request, WINHTTP_QUERY_CONTENT_LENGTH, nullptr,
                                  len_buf, &len_size, WINHTTP_NO_HEADER_INDEX)) {
            total = ::_wcstoui64(len_buf, nullptr, 10);
        }
    }

    HANDLE out = ::CreateFileW(dest_path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        error_out = "CreateFileW(" + util::TextEncoding::to_utf8(dest_path)
                    + ") failed err=" + std::to_string(::GetLastError());
        return false;
    }

    // Any failure past this point deletes the partial file, so a retry
    // cannot pick up a truncated download.
    auto abandon = [&](std::string why) {
        error_out = std::move(why);
        ::CloseHandle(out);
        ::DeleteFileW(dest_path.c_str());
        return false;
    };

    std::uint64_t received = 0;
    auto last_post = ::GetTickCount64();
    std::vector<char> buf(64 * 1024);
    while (true) {
        DWORD got = 0;
        if (!::WinHttpReadData(h.request, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
            return abandon("WinHttpReadData failed err=" + std::to_string(::GetLastError()));
        }
        if (got == 0) {
            break;  // EOF
        }
        DWORD wrote = 0;
        if (!::WriteFile(out, buf.data(), got, &wrote, nullptr) || wrote != got) {
            return abandon("WriteFile failed err=" + std::to_string(::GetLastError()));
        }
        received += got;
        if (const auto now = ::GetTickCount64(); now - last_post >= kProgressIntervalMs) {
            last_post = now;
            if (on_progress) {
                on_progress(received, total);
            }
        }
    }
    ::CloseHandle(out);

    // Final 100% update so the UI lands on a stable end state before the
    // next phase starts.
    if (on_progress) {
        on_progress(received, total > 0 ? total : received);
    }
    return true;
}

}  // namespace lsw::agent::install
