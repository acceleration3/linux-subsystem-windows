#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

namespace {

// The display adapter class, which is where an adapter records what it
// has. An application asks this before it ever opens a driver, and a
// basic display adapter answers zero, which is enough for Photoshop to
// decide there is no GPU worth using.
constexpr const wchar_t* kAdapterClass = L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
                                         L"{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";

// Create-or-open ``root\subkey`` and write one value; false when either
// step fails.
bool write_dword(HKEY root, const wchar_t* subkey, const wchar_t* name, std::uint32_t value) {
    const auto key = win::RegKey::create(root, subkey, KEY_SET_VALUE);
    return key && key->write_dword(name, value) == ERROR_SUCCESS;
}

bool write_qword(HKEY root, const wchar_t* subkey, const wchar_t* name, std::uint64_t value) {
    const auto key = win::RegKey::create(root, subkey, KEY_SET_VALUE);
    return key && key->write_qword(name, value) == ERROR_SUCCESS;
}

}  // namespace

// Apply the host's display settings inside the guest.
//
// Three things with three different lifetimes, which is why the reply
// says which of them actually took:
//
//   * the resolution changes now, through the display driver;
//   * the DPI is read by Windows when a session starts, so it needs a
//     logon, which in practice means the next boot;
//   * the video-memory figure is read by whatever asks the adapter,
//     which means the next time the adapter is enumerated.
//
// None of them is worth a reboot on its own, so nothing here forces
// one. The manager tells the user which of its settings are waiting.
struct SetDisplayHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        nlohmann::json reply = {{"resolution", false}, {"dpi", false}, {"vram", false}};

        const int width = params.value("width", 0);
        const int height = params.value("height", 0);
        if (width > 0 && height > 0) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) != 0) {
                if (static_cast<int>(mode.dmPelsWidth) != width ||
                    static_cast<int>(mode.dmPelsHeight) != height) {
                    mode.dmPelsWidth = static_cast<DWORD>(width);
                    mode.dmPelsHeight = static_cast<DWORD>(height);
                    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
                    reply["resolution"] =
                        ::ChangeDisplaySettingsExW(nullptr, &mode, nullptr, CDS_UPDATEREGISTRY,
                                                   nullptr) == DISP_CHANGE_SUCCESSFUL;
                } else {
                    reply["resolution"] = true;
                }
            }
        }

        // 96 DPI is 100%; Windows stores the scaled figure, and
        // Win8DpiScaling is what tells it to honour a per-user one at
        // all.
        if (const int percent = params.value("scaling_percent", 0); percent > 0) {
            const auto dpi = static_cast<std::uint32_t>((percent * 96) / 100);
            const bool a =
                write_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"LogPixels", dpi);
            const bool b = write_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop",
                                       L"Win8DpiScaling", percent == 100 ? 0U : 1U);
            reply["dpi"] = a && b;
        }

        // Both spellings, because consumers are split between them: the
        // QWORD is what a WDDM adapter reports today and the DWORD is
        // what the older path reads.
        if (const int mib = params.value("vram_mib", 0); mib > 0) {
            const auto bytes = static_cast<std::uint64_t>(mib) * 1024ULL * 1024ULL;
            const bool a = write_qword(HKEY_LOCAL_MACHINE, kAdapterClass,
                                       L"HardwareInformation.qwMemorySize", bytes);
            // The DWORD cannot hold four gigabytes, and truncating to
            // it reports a card with no memory at all — the opposite of
            // what was asked for. Saturate instead: a consumer reading
            // the old spelling sees the largest it can express, and one
            // reading the QWORD sees the real figure.
            constexpr std::uint64_t kDwordMax = 0xFFFFFFFFULL;
            const bool b =
                write_dword(HKEY_LOCAL_MACHINE, kAdapterClass, L"HardwareInformation.MemorySize",
                            static_cast<std::uint32_t>(bytes > kDwordMax ? kDwordMax : bytes));
            reply["vram"] = a && b;
        }
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        std::string out;
        for (const char* key : {"resolution", "dpi", "vram"}) {
            out += std::string{key} + "=" + (reply.value(key, false) ? "1" : "0") + " ";
        }
        return out;
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetDisplayHandler, ::lsw::linkproto::cmd::kSetDisplay)
