#include "lsw/agent/transport/ivshmem_region.hpp"

#include "lsw/linkproto/region_layout.hpp"

#include <windows.h>
// setupapi.h must follow windows.h; initguid.h must precede the
// DEFINE_GUID that gives the interface GUID storage.
#include <initguid.h>
#include <setupapi.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

namespace lsw::agent::transport {

namespace {

// Public IVSHMEM driver interface GUID (virtio-win/ivshmem/Public.h).
// Declared locally so this does not depend on the WDK headers.
// {DF576976-569D-4672-95A0-F57E4EA0B210}
DEFINE_GUID(GUID_DEVINTERFACE_IVSHMEM, 0xdf576976, 0x569d, 0x4672, 0x95, 0xa0, 0xf5, 0x7e, 0x4e,
            0xa0, 0xb2, 0x10);

constexpr DWORD kFileDeviceUnknown = 0x22;
constexpr DWORD kMethodBuffered = 0;
constexpr DWORD kFileAnyAccess = 0;

constexpr DWORD ctl_code(DWORD function) {
    return (kFileDeviceUnknown << 16) | (kFileAnyAccess << 14) | (function << 2) | kMethodBuffered;
}

// Function codes per vendor/ivshmem/ivshmem.h in the Looking Glass tree
// — what the installed Red Hat / virtio-win driver actually serves.
// Numbering starts at 0x800, verified by IOCTL sweep against the live
// driver; 0x801 as the base was an earlier, wrong guess.
constexpr DWORD kIoctlRequestSize = ctl_code(0x801);
constexpr DWORD kIoctlRequestMmap = ctl_code(0x802);
constexpr DWORD kIoctlReleaseMmap = ctl_code(0x803);
constexpr DWORD kIoctlRingDoorbell = ctl_code(0x804);

constexpr UINT8 kCacheCached = 1;
constexpr UINT8 kCacheWriteCombined = 2;

// Which cache mode to ask the driver for.
//
// Write-combining is the fast choice for a region this side only ever
// writes in bulk, and it is what this has always used. It is also the
// one x86 memory type with genuinely weak ordering in both directions,
// so it puts the burden of SFENCE/LFENCE on every publish and consume
// (see linkproto/region_fence.hpp). ``LSW_IVSHMEM_CACHE=cached`` asks
// for ordinary write-back memory instead: slower for large writes,
// strongly ordered, and therefore the control when something in the
// stream looks like it arrived out of order.
[[nodiscard]] UINT8 configured_cache_mode() noexcept {
    const char* v = std::getenv("LSW_IVSHMEM_CACHE");
    if (v != nullptr && std::strcmp(v, "cached") == 0) {
        return kCacheCached;
    }
    return kCacheWriteCombined;
}

struct IvshmemMmapConfig {
    UINT8 cacheMode;
};
struct IvshmemMmap {
    UINT16 peerID;
    UINT64 size;
    PVOID ptr;
    UINT16 vectors;
};
struct IvshmemRing {
    UINT16 peerID;
    UINT16 vector;
};

// The device path of the ``index``-th IVSHMEM function present, or an
// empty string when there is no such device.
[[nodiscard]] std::wstring ivshmem_device_path(DWORD index) {
    HDEVINFO set = ::SetupDiGetClassDevsW(&GUID_DEVINTERFACE_IVSHMEM, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return {};
    }
    SP_DEVICE_INTERFACE_DATA ifdata{};
    ifdata.cbSize = sizeof(ifdata);
    std::wstring path;
    if (::SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_IVSHMEM, index, &ifdata)
        != FALSE) {
        DWORD needed = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &ifdata, nullptr, 0, &needed, nullptr);
        auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
            ::HeapAlloc(::GetProcessHeap(), 0, needed));
        if (detail != nullptr) {
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (::SetupDiGetDeviceInterfaceDetailW(set, &ifdata, detail, needed, nullptr, nullptr)
                != FALSE) {
                path = detail->DevicePath;
            }
            ::HeapFree(::GetProcessHeap(), 0, detail);
        }
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return path;
}

}  // namespace

IvshmemRegion::~IvshmemRegion() {
    close();
}

int IvshmemRegion::open() {
    if (base_ != nullptr) {
        return 0;
    }
    // Every IVSHMEM function on the bus, not the first one.
    //
    // The guest has more than one: this region carries the frame ring
    // and the agent's control channel, and the render server's region —
    // a different device, created by a different process on the host —
    // sits beside it. Which one Windows enumerates first is not ours to
    // decide, so the region is identified by what is written in it
    // rather than by where it turned up.
    int last_error = -ENODEV;
    for (DWORD index = 0;; ++index) {
        const std::wstring path = ivshmem_device_path(index);
        if (path.empty()) {
            return last_error;
        }
        // Shared read/write: the agent has the same device open for the
        // frame channel while other openers use other channels. One
        // region, several channels, several openers — which is what the
        // channel table is for.
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        const int rc = adopt_device(h);
        if (rc == 0) {
            return 0;
        }
        ::CloseHandle(h);
        // -EPROTO says "not ours", which is not an error worth
        // reporting while another device may still be. Anything else is
        // a real failure of a device that IS ours to use.
        if (rc != -EPROTO) {
            last_error = rc;
        }
    }
}

int IvshmemRegion::adopt_device(void* device_handle) {
    HANDLE h = device_handle;
    DWORD got = 0;
    UINT64 dev_size = 0;
    if (::DeviceIoControl(h, kIoctlRequestSize, nullptr, 0, &dev_size, sizeof(dev_size), &got,
                          nullptr)
            == FALSE
        || dev_size == 0) {
        last_os_error_ = ::GetLastError();
        return -ENXIO;
    }

    IvshmemMmapConfig cfg{};
    cfg.cacheMode = configured_cache_mode();
    IvshmemMmap m{};
    if (::DeviceIoControl(h, kIoctlRequestMmap, &cfg, sizeof(cfg), &m, sizeof(m), &got, nullptr)
            == FALSE
        || m.ptr == nullptr) {
        // Most often ERROR_ALREADY_INITIALIZED / ACCESS_DENIED: the
        // driver hands the BAR to one owner at a time.
        last_os_error_ = ::GetLastError();
        return -EBUSY;
    }

    // Check the header before trusting a single offset out of it. A
    // version this build does not know is the expected result of a
    // half-finished redeploy, and reading a v2 channel table as a v1
    // slot table would be far worse than refusing. It is also how a
    // device belonging to something else is told apart from ours.
    if (!linkproto::region_is_readable(m.ptr, m.size)) {
        ::DeviceIoControl(h, kIoctlReleaseMmap, nullptr, 0, nullptr, 0, &got, nullptr);
        return -EPROTO;
    }

    device_handle_ = h;
    base_ = m.ptr;
    size_ = static_cast<std::size_t>(m.size);
    peer_id_ = m.peerID;
    msi_vectors_ = m.vectors;
    return 0;
}

void IvshmemRegion::close() noexcept {
    if (device_handle_ == nullptr) {
        return;
    }
    DWORD got = 0;
    if (base_ != nullptr) {
        ::DeviceIoControl(device_handle_, kIoctlReleaseMmap, nullptr, 0, nullptr, 0, &got,
                          nullptr);
        base_ = nullptr;
    }
    ::CloseHandle(device_handle_);
    device_handle_ = nullptr;
    size_ = 0;
}

void IvshmemRegion::ring_vector(std::uint16_t vector) const noexcept {
    if (device_handle_ == nullptr) {
        return;
    }
    IvshmemRing ring{};
    ring.peerID = 0;  // the manager is peer 0, per the IvshmemServer protocol
    ring.vector = vector;
    DWORD got = 0;
    ::DeviceIoControl(device_handle_, kIoctlRingDoorbell, &ring, sizeof(ring), nullptr, 0, &got,
                      nullptr);
}

}  // namespace lsw::agent::transport
