#include "lsw/agent/transport/ivshmem_region.hpp"

#include "lsw/linkproto/region_layout.hpp"

#include <windows.h>
// setupapi.h must follow windows.h; initguid.h must precede the
// DEFINE_GUID that gives the interface GUID storage.
#include <initguid.h>
#include <setupapi.h>

#include <cerrno>

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

constexpr UINT8 kCacheWriteCombined = 2;

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

[[nodiscard]] HANDLE open_ivshmem_device() {
    HDEVINFO set = ::SetupDiGetClassDevsW(&GUID_DEVINTERFACE_IVSHMEM, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    SP_DEVICE_INTERFACE_DATA ifdata{};
    ifdata.cbSize = sizeof(ifdata);
    if (::SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_IVSHMEM, 0, &ifdata)
        == FALSE) {
        ::SetupDiDestroyDeviceInfoList(set);
        return INVALID_HANDLE_VALUE;
    }
    DWORD needed = 0;
    ::SetupDiGetDeviceInterfaceDetailW(set, &ifdata, nullptr, 0, &needed, nullptr);
    auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
        ::HeapAlloc(::GetProcessHeap(), 0, needed));
    if (detail == nullptr) {
        ::SetupDiDestroyDeviceInfoList(set);
        return INVALID_HANDLE_VALUE;
    }
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    HANDLE h = INVALID_HANDLE_VALUE;
    if (::SetupDiGetDeviceInterfaceDetailW(set, &ifdata, detail, needed, nullptr, nullptr)
        != FALSE) {
        // Shared read/write: the agent has the same device open for the
        // frame channel while the ICD uses the command stream. One
        // region, several channels, several openers — which is what the
        // channel table is for.
        h = ::CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    ::HeapFree(::GetProcessHeap(), 0, detail);
    ::SetupDiDestroyDeviceInfoList(set);
    return h;
}

}  // namespace

IvshmemRegion::~IvshmemRegion() {
    close();
}

int IvshmemRegion::open() {
    if (base_ != nullptr) {
        return 0;
    }
    HANDLE h = open_ivshmem_device();
    if (h == INVALID_HANDLE_VALUE) {
        return -ENODEV;
    }

    DWORD got = 0;
    UINT64 dev_size = 0;
    if (::DeviceIoControl(h, kIoctlRequestSize, nullptr, 0, &dev_size, sizeof(dev_size), &got,
                          nullptr)
            == FALSE
        || dev_size == 0) {
        last_os_error_ = ::GetLastError();
        ::CloseHandle(h);
        return -ENXIO;
    }

    IvshmemMmapConfig cfg{};
    cfg.cacheMode = kCacheWriteCombined;
    IvshmemMmap m{};
    if (::DeviceIoControl(h, kIoctlRequestMmap, &cfg, sizeof(cfg), &m, sizeof(m), &got, nullptr)
            == FALSE
        || m.ptr == nullptr) {
        // Most often ERROR_ALREADY_INITIALIZED / ACCESS_DENIED: the
        // driver hands the BAR to one owner at a time.
        last_os_error_ = ::GetLastError();
        ::CloseHandle(h);
        return -EBUSY;
    }

    // Check the header before trusting a single offset out of it. A
    // version this build does not know is the expected result of a
    // half-finished redeploy, and reading a v2 channel table as a v1
    // slot table would be far worse than refusing.
    if (!linkproto::region_is_readable(m.ptr, m.size)) {
        ::DeviceIoControl(h, kIoctlReleaseMmap, nullptr, 0, nullptr, 0, &got, nullptr);
        ::CloseHandle(h);
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
