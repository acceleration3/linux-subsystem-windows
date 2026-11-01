#include "lsw/agent/apps/apps_folder.hpp"

#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/icon_extractor.hpp"

// clang-format off
#include <knownfolders.h>
#include <propsys.h>
#include <shlguid.h>
#include <shlobj.h>
// clang-format on

#include <stdexcept>

namespace lsw::agent::apps {

namespace {

// System.Link.TargetParsingPath and System.Link.Arguments, spelled out
// here because propkey.h only declares them and the agent's link line has
// nothing that defines them.
constexpr PROPERTYKEY kLinkTargetParsingPath = {
    {0xb9b4b3fc, 0x2b51, 0x4a42, {0xb5, 0xd8, 0x32, 0x41, 0x46, 0xaf, 0xcf, 0x25}}, 2};
constexpr PROPERTYKEY kLinkArguments = {
    {0x436f2667, 0x14e2, 0x4feb, {0xb3, 0x0a, 0x14, 0x6c, 0x53, 0xb5, 0xb6, 0x74}}, 100};

std::wstring display_name(IShellItem& item, SIGDN form) {
    PWSTR text = nullptr;
    if (FAILED(item.GetDisplayName(form, &text)) || text == nullptr) return {};
    std::wstring out{text};
    ::CoTaskMemFree(text);
    return out;
}

std::wstring string_property(IShellItem& item, const PROPERTYKEY& key) {
    win::ComPtr<IShellItem2> item2;
    if (FAILED(item.QueryInterface(IID_PPV_ARGS(item2.GetAddressOf())))) return {};
    PWSTR text = nullptr;
    if (FAILED(item2->GetString(key, &text)) || text == nullptr) return {};
    std::wstring out{text};
    ::CoTaskMemFree(text);
    return out;
}


}  // namespace

std::vector<AppsFolderItem> AppsFolder::items() {
    win::ComPtr<IShellItem> folder;
    HRESULT hr = ::SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                        IID_PPV_ARGS(folder.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("cannot open the Start menu's apps: " + win::ErrorText::hresult(hr));
    win::ComPtr<IEnumShellItems> enumerator;
    hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(enumerator.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("cannot list the Start menu's apps: " + win::ErrorText::hresult(hr));

    std::vector<AppsFolderItem> out;
    for (;;) {
        win::ComPtr<IShellItem> item;
        const HRESULT next = enumerator->Next(1, item.GetAddressOf(), nullptr);
        if (next == S_FALSE) break;  // the end
        // Anything else that is not an item is the shell failing part way
        // through: a shorter list taken for the whole one would have the
        // manager forget applications that are still installed.
        if (next != S_OK) {
            throw std::runtime_error("the Start menu's apps could not be read to the end: " +
                                     win::ErrorText::hresult(next));
        }
        AppsFolderItem entry;
        entry.parsing_name = display_name(*item.Get(), SIGDN_PARENTRELATIVEPARSING);
        entry.name = display_name(*item.Get(), SIGDN_NORMALDISPLAY);
        if (!entry.packaged()) {
            entry.target = string_property(*item.Get(), kLinkTargetParsingPath);
            entry.arguments = string_property(*item.Get(), kLinkArguments);
        }
        entry.shell = std::move(item);
        out.push_back(std::move(entry));
    }
    return out;
}

std::string AppsFolder::icon(IShellItem& item, int size) {
    win::ComPtr<IShellItemImageFactory> images;
    if (FAILED(item.QueryInterface(IID_PPV_ARGS(images.GetAddressOf())))) return {};
    HBITMAP bitmap = nullptr;
    if (FAILED(images->GetImage(SIZE{size, size}, SIIGBF_ICONONLY, &bitmap)) || bitmap == nullptr) {
        return {};
    }
    std::string png = win::IconExtractor::from_premultiplied_bitmap(bitmap);
    ::DeleteObject(bitmap);
    return png;
}

}  // namespace lsw::agent::apps
