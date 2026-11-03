#include "lsw/agent/win/clipboard_watcher.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/image_codec.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstring>
#include <stdexcept>
#include <utility>

namespace lsw::agent::win {

namespace {
constexpr unsigned kWriteMessage = WM_APP + 1;
constexpr int kOpenTries = 10;
constexpr DWORD kOpenRetryMs = 20;
constexpr const wchar_t kClassName[] = L"LswClipboardWatcher";

struct WriteRequest {
    const ClipboardWatcher::Content* content;
    bool ok;
};

UINT png_format() {
    static const UINT format = ::RegisterClipboardFormatW(L"PNG");
    return format;
}

UINT exclude_format() {
    static const UINT format =
        ::RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    return format;
}

UINT history_format() {
    static const UINT format = ::RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    return format;
}

UINT cloud_format() {
    static const UINT format = ::RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
    return format;
}

UINT marker_format() {
    static const UINT format = ::RegisterClipboardFormatW(L"LSW Clipboard Owner");
    return format;
}

bool open_clipboard(HWND owner) {
    for (int i = 0; i < kOpenTries; ++i) {
        if (::OpenClipboard(owner) != FALSE) {
            return true;
        }
        ::Sleep(kOpenRetryMs);
    }
    return false;
}

class OpenedClipboard {
public:
    explicit OpenedClipboard(HWND owner) : open_(open_clipboard(owner)) {}

    ~OpenedClipboard() {
        if (open_) {
            ::CloseClipboard();
        }
    }

    OpenedClipboard(const OpenedClipboard&) = delete;
    OpenedClipboard& operator=(const OpenedClipboard&) = delete;

    [[nodiscard]] bool open() const { return open_; }

private:
    bool open_;
};

std::size_t block_size(UINT format, const void*& data, HGLOBAL& handle) {
    handle = static_cast<HGLOBAL>(::GetClipboardData(format));
    if (handle == nullptr) {
        data = nullptr;
        return 0;
    }
    data = ::GlobalLock(handle);
    return data == nullptr ? 0 : static_cast<std::size_t>(::GlobalSize(handle));
}

bool read_bytes(UINT format, std::size_t cap, std::vector<std::uint8_t>& out, bool& too_large) {
    const void* data = nullptr;
    HGLOBAL handle = nullptr;
    const std::size_t size = block_size(format, data, handle);
    if (data == nullptr) {
        return false;
    }
    bool ok = false;
    if (size > cap) {
        too_large = true;
    } else {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        out.assign(bytes, bytes + size);
        ok = true;
    }
    ::GlobalUnlock(handle);
    return ok;
}

std::string to_crlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            out.push_back('\r');
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string to_lf(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

bool put_block(UINT format, const void* data, std::size_t size) {
    HGLOBAL block = ::GlobalAlloc(GMEM_MOVEABLE, size == 0 ? 1 : size);
    if (block == nullptr) {
        return false;
    }
    void* dest = ::GlobalLock(block);
    if (dest == nullptr) {
        ::GlobalFree(block);
        return false;
    }
    if (size != 0) {
        std::memcpy(dest, data, size);
    }
    ::GlobalUnlock(block);
    if (::SetClipboardData(format, block) == nullptr) {
        ::GlobalFree(block);
        return false;
    }
    return true;
}

}

ClipboardWatcher::ClipboardWatcher() {
    std::promise<bool> ready;
    auto started = ready.get_future();
    thread_ = std::thread{[this, p = std::move(ready)]() mutable { run(std::move(p)); }};
    if (!started.get()) {
        thread_.join();
        throw std::runtime_error("the clipboard watcher window could not be created");
    }
}

ClipboardWatcher::~ClipboardWatcher() {
    if (hwnd_ != nullptr) {
        ::PostMessageW(static_cast<HWND>(hwnd_), WM_CLOSE, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

long long __stdcall ClipboardWatcher::window_proc(void* hwnd, unsigned message,
                                                  std::uintptr_t wparam, std::intptr_t lparam) {
    const auto window = static_cast<HWND>(hwnd);
    auto* self = reinterpret_cast<ClipboardWatcher*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        if (self != nullptr) {
            self->on_update();
        }
        return 0;
    case kWriteMessage:
        if (self != nullptr) {
            auto* request = reinterpret_cast<WriteRequest*>(lparam);
            request->ok = self->write_on_thread(*request->content);
        }
        return 0;
    case WM_CLOSE:
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ::RemoveClipboardFormatListener(window);
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}

void ClipboardWatcher::run(std::promise<bool> ready) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&ClipboardWatcher::window_proc);
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);
    const HWND window = ::CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                          wc.hInstance, nullptr);
    if (window == nullptr) {
        LSW_AGENT_WARN("clipboard: no message window: {}", ErrorText::win32(::GetLastError()));
        ready.set_value(false);
        return;
    }
    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    if (::AddClipboardFormatListener(window) == FALSE) {
        LSW_AGENT_WARN("clipboard: no format listener: {}", ErrorText::win32(::GetLastError()));
        ::DestroyWindow(window);
        ready.set_value(false);
        return;
    }
    hwnd_ = window;
    ready.set_value(true);
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    hwnd_ = nullptr;
}

bool ClipboardWatcher::is_secret() const {
    if (::IsClipboardFormatAvailable(exclude_format()) != FALSE) {
        return true;
    }
    if (::IsClipboardFormatAvailable(history_format()) == FALSE) {
        return false;
    }
    const void* data = nullptr;
    HGLOBAL handle = nullptr;
    const std::size_t size = block_size(history_format(), data, handle);
    bool secret = false;
    if (data != nullptr) {
        DWORD value = 1;
        if (size >= sizeof(value)) {
            std::memcpy(&value, data, sizeof(value));
        }
        secret = value == 0;
        ::GlobalUnlock(handle);
    }
    return secret;
}

void ClipboardWatcher::on_update() {
    if (::IsClipboardFormatAvailable(marker_format()) != FALSE) {
        return;
    }
    const bool text = ::IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
    const bool image = ::IsClipboardFormatAvailable(png_format()) != FALSE ||
                       ::IsClipboardFormatAvailable(CF_DIB) != FALSE ||
                       ::IsClipboardFormatAvailable(CF_DIBV5) != FALSE;
    bool secret = false;
    if (text || image) {
        const OpenedClipboard clipboard{static_cast<HWND>(hwnd_)};
        secret = clipboard.open() && is_secret();
    }
    LSW_AGENT_DEBUG("clipboard: changed text={} image={} secret={}", text, image, secret);
    const std::lock_guard lock(mu_);
    pending_ = nlohmann::json{{"text", text}, {"image", image}, {"secret", secret}};
}

std::vector<rpc::Event> ClipboardWatcher::collect_pending() {
    std::vector<rpc::Event> out;
    const std::lock_guard lock(mu_);
    if (pending_) {
        out.emplace_back(linkproto::topic::kClipboardChanged, std::move(*pending_));
        pending_.reset();
    }
    return out;
}

nlohmann::json ClipboardWatcher::read() {
    nlohmann::json out = nlohmann::json::object();
    std::string text;
    std::vector<std::uint8_t> png;
    std::vector<std::uint8_t> dib;
    bool have_dib = false;
    bool too_large = false;
    bool secret = false;
    {
        const OpenedClipboard clipboard{static_cast<HWND>(hwnd_)};
        if (!clipboard.open()) {
            throw std::runtime_error("the clipboard is held by another application");
        }
        secret = is_secret();
        if (::IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE) {
            const void* data = nullptr;
            HGLOBAL handle = nullptr;
            const std::size_t size = block_size(CF_UNICODETEXT, data, handle);
            if (data != nullptr) {
                std::wstring wide(static_cast<const wchar_t*>(data), size / sizeof(wchar_t));
                ::GlobalUnlock(handle);
                const auto nul = wide.find(L'\0');
                if (nul != std::wstring::npos) {
                    wide.resize(nul);
                }
                std::string utf8 = to_lf(util::TextEncoding::to_utf8(wide));
                if (utf8.size() > kMaxTextBytes) {
                    too_large = true;
                } else {
                    text = std::move(utf8);
                    out["text"] = text;
                }
            }
        }
        const bool have_png = ::IsClipboardFormatAvailable(png_format()) != FALSE &&
                              read_bytes(png_format(), kMaxImageBytes, png, too_large);
        if (!have_png) {
            const UINT dib_format =
                ::IsClipboardFormatAvailable(CF_DIB) != FALSE ? CF_DIB : CF_DIBV5;
            if (::IsClipboardFormatAvailable(dib_format) != FALSE &&
                read_bytes(dib_format, ImageCodec::kMaxDibBytes, dib, too_large)) {
                have_dib = true;
            }
        }
    }
    if (have_dib) {
        png = ImageCodec::dib_to_png(dib.data(), dib.size());
        dib.clear();
        if (png.size() > kMaxImageBytes) {
            png.clear();
            too_large = true;
        }
    }
    if (!png.empty()) {
        out["png"] = util::Base64::encode(png);
    }
    if (out.dump().size() > kMaxMessageChars && out.contains("png")) {
        LSW_AGENT_DEBUG("clipboard: image of {} bytes dropped, message over budget (text {})",
                        png.size(), text.size());
        out.erase("png");
        too_large = true;
    }
    if (out.dump().size() > kMaxMessageChars) {
        LSW_AGENT_DEBUG("clipboard: text of {} bytes dropped, message over budget", text.size());
        out.erase("text");
        too_large = true;
    }
    if (too_large) {
        out["too_large"] = true;
    }
    out["secret"] = secret;
    LSW_AGENT_DEBUG("clipboard: read text={} png={} too_large={} secret={}", out.contains("text"),
                    out.contains("png"), too_large, secret);
    return out;
}

void ClipboardWatcher::write(const Content& content) {
    WriteRequest request{&content, false};
    ::SendMessageW(static_cast<HWND>(hwnd_), kWriteMessage, 0, reinterpret_cast<LPARAM>(&request));
    if (!request.ok) {
        throw std::runtime_error("the clipboard could not be written");
    }
}

bool ClipboardWatcher::write_on_thread(const Content& content) {
    const OpenedClipboard clipboard{static_cast<HWND>(hwnd_)};
    if (!clipboard.open()) {
        return false;
    }
    if (content.clear) {
        if (::IsClipboardFormatAvailable(marker_format()) == FALSE) {
            return true;
        }
        return ::EmptyClipboard() != FALSE;
    }
    if (::EmptyClipboard() == FALSE) {
        return false;
    }
    bool ok = true;
    if (content.text) {
        const std::wstring wide = util::TextEncoding::to_wide(to_crlf(*content.text));
        ok = put_block(CF_UNICODETEXT, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t)) && ok;
    }
    if (content.png) {
        ok = put_block(png_format(), content.png->data(), content.png->size()) && ok;
    }
    if (content.dib) {
        ok = put_block(CF_DIB, content.dib->data(), content.dib->size()) && ok;
    }
    if (content.secret) {
        const DWORD zero = 0;
        ok = put_block(exclude_format(), nullptr, 0) && ok;
        ok = put_block(history_format(), &zero, sizeof(zero)) && ok;
        ok = put_block(cloud_format(), &zero, sizeof(zero)) && ok;
    }
    const std::uint8_t mark = 1;
    ok = put_block(marker_format(), &mark, 1) && ok;
    LSW_AGENT_DEBUG("clipboard: wrote text={} png={} dib={} secret={} ok={}",
                    content.text.has_value(), content.png.has_value(), content.dib.has_value(),
                    content.secret, ok);
    return ok;
}

}
