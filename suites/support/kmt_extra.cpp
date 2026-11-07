#include "support/kmt_extra.hpp"

#include <cstring>

namespace lsw::suites {

namespace {

struct WaitVblank {
    std::uint32_t adapter;
    std::uint32_t device;
    std::uint32_t source;
};

struct ScanLine {
    std::uint32_t adapter;
    std::uint32_t source;
    BOOLEAN in_vblank;
    std::uint32_t line;
};

struct Ownership {
    std::uint32_t adapter;
    std::uint32_t source;
};

struct OpenHdc {
    HDC dc;
    std::uint32_t adapter;
    LUID luid;
    std::uint32_t source;
};

struct OpenName {
    wchar_t name[32];
    std::uint32_t adapter;
    LUID luid;
    std::uint32_t source;
};

struct Escape {
    std::uint32_t adapter;
    std::uint32_t device;
    std::uint32_t type;
    std::uint32_t flags;
    void* data;
    std::uint32_t size;
    std::uint32_t context;
};

}  // namespace

bool KmtExtra::load(std::string& error) {
    if (!gdi_.load("gdi32.dll")) {
        error = "gdi32.dll cannot be loaded";
        return false;
    }
    wait_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTWaitForVerticalBlankEvent");
    scan_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTGetScanLine");
    owner_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTCheckVidPnExclusiveOwnership");
    hdc_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTOpenAdapterFromHdc");
    name_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTOpenAdapterFromGdiDisplayName");
    escape_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTEscape");
    if (wait_ == nullptr || scan_ == nullptr || owner_ == nullptr || hdc_ == nullptr || name_ == nullptr || escape_ == nullptr) {
        error = "gdi32 lacks a D3DKMT display entry point";
        return false;
    }
    return true;
}

std::int32_t KmtExtra::wait_vblank(std::uint32_t adapter, std::uint32_t source) const {
    WaitVblank args{adapter, 0, source};
    return wait_(&args);
}

std::int32_t KmtExtra::scan_line(std::uint32_t adapter, std::uint32_t source, bool& in_vblank, std::uint32_t& line) const {
    ScanLine args{adapter, source, FALSE, 0};
    const std::int32_t status = scan_(&args);
    in_vblank = args.in_vblank != FALSE;
    line = args.line;
    return status;
}

std::int32_t KmtExtra::exclusive_ownership(std::uint32_t adapter, std::uint32_t source) const {
    Ownership args{adapter, source};
    return owner_(&args);
}

std::int32_t KmtExtra::open_from_hdc(HDC dc, std::uint32_t& adapter, LUID& luid, std::uint32_t& source) const {
    OpenHdc args{dc, 0, {}, 0};
    const std::int32_t status = hdc_(&args);
    adapter = args.adapter;
    luid = args.luid;
    source = args.source;
    return status;
}

std::int32_t KmtExtra::open_from_display_name(const std::wstring& name, std::uint32_t& adapter, LUID& luid, std::uint32_t& source) const {
    OpenName args{};
    std::wcsncpy(args.name, name.c_str(), 31);
    const std::int32_t status = name_(&args);
    adapter = args.adapter;
    luid = args.luid;
    source = args.source;
    return status;
}

std::int32_t KmtExtra::escape(std::uint32_t adapter, std::uint32_t type, std::uint32_t flags, void* data, std::uint32_t size) const {
    Escape args{adapter, 0, type, flags, data, size, 0};
    return escape_(&args);
}

}  // namespace lsw::suites
