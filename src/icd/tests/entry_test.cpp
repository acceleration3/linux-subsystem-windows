// Smoke-test the ICD shim by dlopen'ing the freshly-built lsw_icd.so and
// checking that the negotiation + entry resolution work the way the Vulkan
// loader will exercise them. We deliberately do *not* link against
// vulkan-loader here — we want to test the ICD's contract in isolation.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <vulkan/vulkan_core.h>

#ifndef LSW_ICD_PATH
#error "LSW_ICD_PATH not provided by build system"
#endif

namespace {

void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

using PFN_NegotiateVersion = VkResult(VKAPI_PTR*)(uint32_t*);
using PFN_GetInstanceProcAddr = PFN_vkVoidFunction(VKAPI_PTR*)(VkInstance, const char*);

}  // namespace

int main() {
    void* handle = ::dlopen(LSW_ICD_PATH, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "dlopen %s failed: %s\n", LSW_ICD_PATH, ::dlerror());
        return 1;
    }

    auto negotiate = reinterpret_cast<PFN_NegotiateVersion>(
        ::dlsym(handle, "vk_icdNegotiateLoaderICDInterfaceVersion"));
    check(negotiate != nullptr, "vk_icdNegotiateLoaderICDInterfaceVersion exported");

    auto gipa =
        reinterpret_cast<PFN_GetInstanceProcAddr>(::dlsym(handle, "vk_icdGetInstanceProcAddr"));
    check(gipa != nullptr, "vk_icdGetInstanceProcAddr exported");

    // Negotiate the loader interface version.
    uint32_t version = 7;
    VkResult res = negotiate(&version);
    check(res == VK_SUCCESS, "negotiate returned success");
    check(version <= 7 && version >= 1, "negotiated version is in a sane range");

    // The loader queries these names before vkCreateInstance.
    const char* expected[] = {
        "vkEnumerateInstanceVersion",
        "vkEnumerateInstanceExtensionProperties",
        "vkEnumerateInstanceLayerProperties",
        "vkCreateInstance",
        "vkDestroyInstance",
        "vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceProperties2",
        "vkCreateDevice",
        "vkDestroyDevice",
        "vkGetDeviceQueue",
    };
    for (const char* name : expected) {
        auto fn = gipa(VK_NULL_HANDLE, name);
        if (fn == nullptr) {
            std::fprintf(stderr, "FAIL: %s not exposed via vk_icdGetInstanceProcAddr\n", name);
            std::exit(1);
        }
    }

    // Unknown name returns null.
    check(gipa(VK_NULL_HANDLE, "vkBogusFunctionName") == nullptr, "unknown name returns null");
    check(gipa(VK_NULL_HANDLE, nullptr) == nullptr, "nullptr name handled");

    // The loader-level no-op exports work without a server (don't go on the wire).
    auto enum_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    uint32_t api = 0;
    res = enum_version(&api);
    check(res == VK_SUCCESS, "vkEnumerateInstanceVersion succeeds");
    check(api >= VK_API_VERSION_1_0, "reports a sane API version");

    auto enum_ext = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    uint32_t count = 0;
    res = enum_ext(nullptr, &count, nullptr);
    check(res == VK_SUCCESS, "EnumerateInstanceExtensionProperties query succeeds");
    check(count > 0, "ICD reports at least one instance extension");

    // Surface entry points must resolve through vk_icdGetInstanceProcAddr.
    check(gipa(VK_NULL_HANDLE, "vkCreateXcbSurfaceKHR") != nullptr,
          "vkCreateXcbSurfaceKHR exposed");
    check(gipa(VK_NULL_HANDLE, "vkDestroySurfaceKHR") != nullptr, "vkDestroySurfaceKHR exposed");

    ::dlclose(handle);
    std::puts("OK");
    return 0;
}
