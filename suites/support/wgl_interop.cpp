#include "support/wgl_interop.hpp"

namespace lsw::suites {

void WglInterop::load(const GlLibrary& library) {
#define LSW_WGL_LOAD(member, name) member = reinterpret_cast<decltype(member)>(library.proc(name))
    LSW_WGL_LOAD(open_device, "wglDXOpenDeviceNV");
    LSW_WGL_LOAD(close_device, "wglDXCloseDeviceNV");
    LSW_WGL_LOAD(register_object, "wglDXRegisterObjectNV");
    LSW_WGL_LOAD(unregister_object, "wglDXUnregisterObjectNV");
    LSW_WGL_LOAD(object_access, "wglDXObjectAccessNV");
    LSW_WGL_LOAD(lock_objects, "wglDXLockObjectsNV");
    LSW_WGL_LOAD(unlock_objects, "wglDXUnlockObjectsNV");
    LSW_WGL_LOAD(set_share_handle, "wglDXSetResourceShareHandleNV");
#undef LSW_WGL_LOAD
}

bool WglInterop::complete() const {
    return open_device != nullptr && close_device != nullptr && register_object != nullptr && unregister_object != nullptr && object_access != nullptr && lock_objects != nullptr &&
           unlock_objects != nullptr && set_share_handle != nullptr;
}

std::string WglInterop::missing() const {
    std::string out;
    const struct {
        const char* name;
        bool present;
    } list[] = {{"wglDXOpenDeviceNV", open_device != nullptr},           {"wglDXCloseDeviceNV", close_device != nullptr},
                {"wglDXRegisterObjectNV", register_object != nullptr},   {"wglDXUnregisterObjectNV", unregister_object != nullptr},
                {"wglDXObjectAccessNV", object_access != nullptr},       {"wglDXLockObjectsNV", lock_objects != nullptr},
                {"wglDXUnlockObjectsNV", unlock_objects != nullptr},     {"wglDXSetResourceShareHandleNV", set_share_handle != nullptr}};
    for (const auto& item : list) {
        if (!item.present) {
            out += (out.empty() ? "" : ", ") + std::string(item.name);
        }
    }
    return out;
}

}  // namespace lsw::suites
