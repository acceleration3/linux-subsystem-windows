#include "support/kmt_api.hpp"

#include "core/text.hpp"

#include <cstring>

namespace lsw::suites {

namespace {

struct AdapterInfo {
    std::uint32_t handle;
    LUID luid;
    std::uint32_t sources;
    BOOL present_move_regions_preferred;
};

struct EnumAdapters2 {
    std::uint32_t count;
    AdapterInfo* adapters;
};

struct OpenFromLuid {
    LUID luid;
    std::uint32_t handle;
};

struct CloseAdapter {
    std::uint32_t handle;
};

struct QueryAdapterInfo {
    std::uint32_t handle;
    std::uint32_t type;
    void* data;
    std::uint32_t size;
};

struct SegmentSizeInfo {
    std::uint64_t dedicated_video;
    std::uint64_t dedicated_system;
    std::uint64_t shared_system;
};

struct AdapterAddress {
    std::uint32_t bus;
    std::uint32_t device;
    std::uint32_t function;
    std::uint32_t reserved;
};

struct UmdFileName {
    std::uint32_t version;
    wchar_t name[MAX_PATH];
};

struct AdapterRegistryInfo {
    wchar_t adapter_string[MAX_PATH];
    wchar_t bios_string[MAX_PATH];
    wchar_t dac_type[MAX_PATH];
    wchar_t chip_type[MAX_PATH];
};

constexpr std::uint32_t kTypeUmdName = 1;
constexpr std::uint32_t kTypeSegmentSize = 3;
constexpr std::uint32_t kTypeAdapterAddress = 6;
constexpr std::uint32_t kTypeRegistryInfo = 8;

}  // namespace

bool KmtApi::load(std::string& error) {
    if (!gdi_.load("gdi32.dll")) {
        error = "gdi32.dll cannot be loaded";
        return false;
    }
    enum_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTEnumAdapters2");
    close_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTCloseAdapter");
    open_luid_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTOpenAdapterFromLuid");
    query_ = gdi_.symbol<std::int32_t(__stdcall*)(void*)>("D3DKMTQueryAdapterInfo");
    if (enum_ == nullptr || close_ == nullptr || open_luid_ == nullptr || query_ == nullptr) {
        error = "gdi32 lacks the D3DKMT adapter entry points";
        return false;
    }
    return true;
}

bool KmtApi::enum_adapters(std::vector<KmtAdapter>& out, std::string& error) const {
    for (int attempt = 0; attempt < 4; ++attempt) {
        EnumAdapters2 sizing{0, nullptr};
        const std::int32_t first = enum_(&sizing);
        if (first != 0 && first != kStatusBufferTooSmall) {
            error = "D3DKMTEnumAdapters2 sizing failed: 0x" +
                    Text::hex(static_cast<std::uint32_t>(first), 8);
            return false;
        }
        std::vector<AdapterInfo> infos(sizing.count == 0 ? 1 : sizing.count);
        EnumAdapters2 args{static_cast<std::uint32_t>(infos.size()), infos.data()};
        const std::int32_t status = enum_(&args);
        if (status == kStatusBufferTooSmall) {
            continue;
        }
        if (status != 0) {
            error = "D3DKMTEnumAdapters2 failed: 0x" + Text::hex(static_cast<std::uint32_t>(status), 8);
            return false;
        }
        for (std::uint32_t i = 0; i < args.count; ++i) {
            out.push_back({infos[i].handle, infos[i].luid, infos[i].sources});
            CloseAdapter close_args{infos[i].handle};
            close_(&close_args);
        }
        return true;
    }
    error = "D3DKMTEnumAdapters2 kept reporting a buffer that is too small";
    return false;
}

std::int32_t KmtApi::open_from_luid(const LUID& luid, std::uint32_t& handle) const {
    OpenFromLuid args{luid, 0};
    const std::int32_t status = open_luid_(&args);
    handle = args.handle;
    return status;
}

std::int32_t KmtApi::close(std::uint32_t handle) const {
    CloseAdapter args{handle};
    return close_(&args);
}

std::int32_t KmtApi::query(std::uint32_t handle, std::uint32_t type, void* data,
                           std::uint32_t size) const {
    QueryAdapterInfo args{handle, type, data, size};
    return query_(&args);
}

namespace {

template <typename Result, typename Fill>
bool with_adapter(const KmtApi& api, const LUID& luid, Result& result, Fill fill) {
    std::uint32_t handle = 0;
    if (api.open_from_luid(luid, handle) != 0) {
        return false;
    }
    const bool ok = fill(handle, result);
    static_cast<void>(api.close(handle));
    return ok;
}

}  // namespace

bool KmtApi::segment_sizes(const LUID& luid, KmtSegmentSizes& out) const {
    return with_adapter(*this, luid, out, [this](std::uint32_t handle, KmtSegmentSizes& result) {
        SegmentSizeInfo info{};
        if (query(handle, kTypeSegmentSize, &info, sizeof(info)) != 0) {
            return false;
        }
        result = {info.dedicated_video, info.dedicated_system, info.shared_system};
        return true;
    });
}

bool KmtApi::address(const LUID& luid, KmtAddress& out) const {
    return with_adapter(*this, luid, out, [this](std::uint32_t handle, KmtAddress& result) {
        AdapterAddress info{};
        if (query(handle, kTypeAdapterAddress, &info, sizeof(info)) != 0) {
            return false;
        }
        result = {info.bus, info.device, info.function};
        return true;
    });
}

bool KmtApi::adapter_string(const LUID& luid, std::wstring& out) const {
    return with_adapter(*this, luid, out, [this](std::uint32_t handle, std::wstring& result) {
        AdapterRegistryInfo info{};
        if (query(handle, kTypeRegistryInfo, &info, sizeof(info)) != 0) {
            return false;
        }
        info.adapter_string[MAX_PATH - 1] = L'\0';
        result = info.adapter_string;
        return true;
    });
}

std::wstring KmtApi::umd_name(const LUID& luid, std::uint32_t version) const {
    std::wstring name;
    with_adapter(*this, luid, name, [this, version](std::uint32_t handle, std::wstring& result) {
        UmdFileName info{};
        info.version = version;
        if (query(handle, kTypeUmdName, &info, sizeof(info)) != 0) {
            return false;
        }
        info.name[MAX_PATH - 1] = L'\0';
        result = info.name;
        return true;
    });
    return name;
}

}  // namespace lsw::suites
