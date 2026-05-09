// Round-trip integration test for the Phase 1 codegen.
//
// Drives generated client wrappers through a LoopbackTransport into a
// hand-written ServerDispatch impl that records what it received and
// replies with known values. Asserts the bytes round-trip cleanly for
// async, sync-with-handle-out, and the two-call enumeration idiom.

#include "lsw/vkproto/loopback.hpp"
#include "vk_default_dispatch.h"
#include "vk_dispatch_client.h"
#include "vk_dispatch_server.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Runtime-initialized; reinterpret_cast is not constexpr.
const auto kFakeInstance =
    reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0xAABBCCDDEEFF0011ULL));
const auto kFakeDevice =
    reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(0x1122334455667788ULL));
const auto kFakeQueue =
    reinterpret_cast<VkQueue>(static_cast<std::uintptr_t>(0xDEADBEEFCAFEBABEULL));
const auto kFakePhysDev0 =
    reinterpret_cast<VkPhysicalDevice>(static_cast<std::uintptr_t>(0x0000000011111111ULL));
const auto kFakePhysDev1 =
    reinterpret_cast<VkPhysicalDevice>(static_cast<std::uintptr_t>(0x0000000022222222ULL));

struct RecordingServer final : lsw::vkproto::DefaultServerDispatch {
    bool destroyed_instance = false;
    bool destroyed_device = false;
    VkInstance last_destroy_instance = VK_NULL_HANDLE;
    VkDevice last_destroy_device = VK_NULL_HANDLE;

    VkDevice queue_device = VK_NULL_HANDLE;
    std::uint32_t queue_qfi = 0;
    std::uint32_t queue_qi = 0;

    VkInstance enum_instance = VK_NULL_HANDLE;

    VkPhysicalDevice props_phys = VK_NULL_HANDLE;
    VkStructureType props_received_stype = VK_STRUCTURE_TYPE_APPLICATION_INFO;

    // vkCreateInstance state
    bool create_instance_called = false;
    std::string ci_app_name;
    std::string ci_engine_name;
    std::uint32_t ci_app_version = 0;
    std::uint32_t ci_api_version = 0;
    std::vector<std::string> ci_extensions;
    std::vector<std::string> ci_layers;
    bool ci_app_info_present = false;

    // vkCreateDevice state
    bool create_device_called = false;
    VkPhysicalDevice cd_phys = VK_NULL_HANDLE;
    std::vector<std::uint32_t> cd_queue_families;
    std::vector<std::vector<float>> cd_queue_priorities;
    std::vector<std::string> cd_extensions;
    bool cd_features_present = false;
    VkBool32 cd_features_geometry_shader = VK_FALSE;

    VkResult on_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* /*pAllocator*/,
                                 VkInstance* pInstance) override {
        create_instance_called = true;
        ci_app_info_present = (pCreateInfo->pApplicationInfo != nullptr);
        if (ci_app_info_present) {
            const auto& app = *pCreateInfo->pApplicationInfo;
            ci_app_name = app.pApplicationName ? app.pApplicationName : "";
            ci_engine_name = app.pEngineName ? app.pEngineName : "";
            ci_app_version = app.applicationVersion;
            ci_api_version = app.apiVersion;
        }
        for (std::uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            ci_extensions.emplace_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }
        for (std::uint32_t i = 0; i < pCreateInfo->enabledLayerCount; ++i) {
            ci_layers.emplace_back(pCreateInfo->ppEnabledLayerNames[i]);
        }
        *pInstance = kFakeInstance;
        return VK_SUCCESS;
    }

    VkResult on_vkCreateDevice(VkPhysicalDevice physicalDevice,
                               const VkDeviceCreateInfo* pCreateInfo,
                               const VkAllocationCallbacks* /*pAllocator*/,
                               VkDevice* pDevice) override {
        create_device_called = true;
        cd_phys = physicalDevice;
        for (std::uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i) {
            const auto& q = pCreateInfo->pQueueCreateInfos[i];
            cd_queue_families.push_back(q.queueFamilyIndex);
            cd_queue_priorities.emplace_back(q.pQueuePriorities, q.pQueuePriorities + q.queueCount);
        }
        for (std::uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            cd_extensions.emplace_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }
        cd_features_present = (pCreateInfo->pEnabledFeatures != nullptr);
        if (cd_features_present) {
            cd_features_geometry_shader = pCreateInfo->pEnabledFeatures->geometryShader;
        }
        *pDevice = kFakeDevice;
        return VK_SUCCESS;
    }

    void on_vkDestroyInstance(VkInstance instance,
                              const VkAllocationCallbacks* /*pAllocator*/) override {
        destroyed_instance = true;
        last_destroy_instance = instance;
    }

    void on_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* /*pAllocator*/) override {
        destroyed_device = true;
        last_destroy_device = device;
    }

    void on_vkGetDeviceQueue(VkDevice device, std::uint32_t queueFamilyIndex,
                             std::uint32_t queueIndex, VkQueue* pQueue) override {
        queue_device = device;
        queue_qfi = queueFamilyIndex;
        queue_qi = queueIndex;
        *pQueue = kFakeQueue;
    }

    void on_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                           VkPhysicalDeviceProperties2* pProperties) override {
        props_phys = physicalDevice;
        props_received_stype = pProperties->sType;
        // Server fills the struct as if it queried real Vulkan.
        pProperties->properties.apiVersion = VK_API_VERSION_1_3;
        pProperties->properties.driverVersion = 0xC0FFEE;
        pProperties->properties.vendorID = 0xCAFE;
        pProperties->properties.deviceID = 0xBABE;
        pProperties->properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        // deviceName: ASCII followed by zeros (struct already zero-initialized server-side).
        const char kName[] = "LSW Loopback GPU";
        for (std::size_t i = 0; i < sizeof(kName); ++i) {
            pProperties->properties.deviceName[i] = kName[i];
        }
        for (std::size_t i = 0; i < VK_UUID_SIZE; ++i) {
            pProperties->properties.pipelineCacheUUID[i] = static_cast<std::uint8_t>(i);
        }
        // Fill a couple of distinctive limits so the test can verify
        // round-tripping of nested-struct-by-value.
        pProperties->properties.limits.maxImageDimension2D = 16384;
        pProperties->properties.limits.maxComputeWorkGroupCount[0] = 65535;
        pProperties->properties.limits.maxComputeWorkGroupCount[1] = 65535;
        pProperties->properties.limits.maxComputeWorkGroupCount[2] = 65535;
        pProperties->properties.limits.pointSizeRange[0] = 1.0f;
        pProperties->properties.limits.pointSizeRange[1] = 64.0f;
        pProperties->properties.sparseProperties.residencyStandard2DBlockShape = VK_TRUE;
    }

    VkResult on_vkEnumeratePhysicalDevices(VkInstance instance, std::uint32_t* pPhysicalDeviceCount,
                                           VkPhysicalDevice* pPhysicalDevices) override {
        enum_instance = instance;
        constexpr std::uint32_t kAvailable = 2;
        if (pPhysicalDevices == nullptr) {
            *pPhysicalDeviceCount = kAvailable;
            return VK_SUCCESS;
        }
        const std::uint32_t to_write =
            (*pPhysicalDeviceCount < kAvailable) ? *pPhysicalDeviceCount : kAvailable;
        if (to_write >= 1) {
            pPhysicalDevices[0] = kFakePhysDev0;
        }
        if (to_write >= 2) {
            pPhysicalDevices[1] = kFakePhysDev1;
        }
        *pPhysicalDeviceCount = to_write;
        return (to_write < kAvailable) ? VK_INCOMPLETE : VK_SUCCESS;
    }
};

void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

void test_destroy_instance() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};
    lsw::vkproto::lsw_vkDestroyInstance(t, kFakeInstance, nullptr);
    check(s.destroyed_instance, "vkDestroyInstance reached server");
    check(s.last_destroy_instance == kFakeInstance, "instance handle round-tripped");
}

void test_destroy_device() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};
    lsw::vkproto::lsw_vkDestroyDevice(t, kFakeDevice, nullptr);
    check(s.destroyed_device, "vkDestroyDevice reached server");
    check(s.last_destroy_device == kFakeDevice, "device handle round-tripped");
}

void test_get_device_queue() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};
    VkQueue q = VK_NULL_HANDLE;
    lsw::vkproto::lsw_vkGetDeviceQueue(t, kFakeDevice, 7, 13, &q);
    check(s.queue_device == kFakeDevice, "device round-tripped");
    check(s.queue_qfi == 7, "queueFamilyIndex round-tripped");
    check(s.queue_qi == 13, "queueIndex round-tripped");
    check(q == kFakeQueue, "out queue round-tripped");
}

void test_enumerate_query_then_fetch() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};

    // First call: query count.
    std::uint32_t count = 0;
    VkResult res = lsw::vkproto::lsw_vkEnumeratePhysicalDevices(t, kFakeInstance, &count, nullptr);
    check(res == VK_SUCCESS, "query call returns success");
    check(s.enum_instance == kFakeInstance, "instance round-tripped on query");
    check(count == 2, "server reported 2 devices");

    // Second call: fetch devices.
    std::vector<VkPhysicalDevice> devs(count, VK_NULL_HANDLE);
    res = lsw::vkproto::lsw_vkEnumeratePhysicalDevices(t, kFakeInstance, &count, devs.data());
    check(res == VK_SUCCESS, "fetch call returns success");
    check(count == 2, "fetched 2 devices");
    check(devs[0] == kFakePhysDev0, "phys dev 0 round-tripped");
    check(devs[1] == kFakePhysDev1, "phys dev 1 round-tripped");
}

void test_enumerate_truncated_fetch() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};

    std::uint32_t count = 1;  // ask for only 1 even though server has 2
    std::vector<VkPhysicalDevice> devs(1, VK_NULL_HANDLE);
    VkResult res =
        lsw::vkproto::lsw_vkEnumeratePhysicalDevices(t, kFakeInstance, &count, devs.data());
    check(res == VK_INCOMPLETE, "VK_INCOMPLETE when buffer too small");
    check(count == 1, "count clamped to capacity");
    check(devs[0] == kFakePhysDev0, "first device round-tripped");
}

void test_get_physical_device_properties2() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};

    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = nullptr;

    lsw::vkproto::lsw_vkGetPhysicalDeviceProperties2(t, kFakePhysDev0, &props);

    check(s.props_phys == kFakePhysDev0, "physical device handle round-tripped");
    check(s.props_received_stype == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
          "sType forwarded to server");
    check(props.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
          "sType preserved on the way back");
    check(props.pNext == nullptr, "pNext stays null");
    check(props.properties.apiVersion == VK_API_VERSION_1_3, "apiVersion round-tripped");
    check(props.properties.vendorID == 0xCAFEu, "vendorID round-tripped");
    check(props.properties.deviceID == 0xBABEu, "deviceID round-tripped");
    check(props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
          "deviceType (enum) round-tripped");
    check(std::strncmp(props.properties.deviceName, "LSW Loopback GPU", 16) == 0,
          "deviceName fixed array round-tripped");
    check(props.properties.pipelineCacheUUID[0] == 0, "pipelineCacheUUID[0] round-tripped");
    check(props.properties.pipelineCacheUUID[15] == 15, "pipelineCacheUUID[15] round-tripped");
    check(props.properties.limits.maxImageDimension2D == 16384,
          "nested struct member round-tripped");
    check(props.properties.limits.maxComputeWorkGroupCount[2] == 65535,
          "nested struct fixed array round-tripped");
    check(props.properties.limits.pointSizeRange[1] == 64.0F,
          "nested struct float fixed array round-tripped");
    check(props.properties.sparseProperties.residencyStandard2DBlockShape == VK_TRUE,
          "doubly-nested struct member round-tripped");
}

void test_create_instance_with_app_info_and_extensions() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkcube";
    app.applicationVersion = 0xABCDu;
    app.pEngineName = "Acme Engine";
    app.engineVersion = 0xDEFu;
    app.apiVersion = VK_API_VERSION_1_3;

    const char* exts[] = {"VK_KHR_surface", "VK_KHR_wayland_surface"};
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = lsw::vkproto::lsw_vkCreateInstance(t, &ci, nullptr, &instance);

    check(res == VK_SUCCESS, "vkCreateInstance returned success");
    check(instance == kFakeInstance, "instance handle round-tripped");
    check(s.create_instance_called, "server saw the call");
    check(s.ci_app_info_present, "pApplicationInfo presence flag round-tripped");
    check(s.ci_app_name == "vkcube", "application name round-tripped");
    check(s.ci_engine_name == "Acme Engine", "engine name round-tripped");
    check(s.ci_app_version == 0xABCDu, "applicationVersion round-tripped");
    check(s.ci_api_version == VK_API_VERSION_1_3, "apiVersion round-tripped");
    check(s.ci_extensions.size() == 2, "two extensions");
    check(s.ci_extensions[0] == "VK_KHR_surface", "ext 0");
    check(s.ci_extensions[1] == "VK_KHR_wayland_surface", "ext 1");
    check(s.ci_layers.size() == 1, "one layer");
    check(s.ci_layers[0] == "VK_LAYER_KHRONOS_validation", "layer 0");
}

void test_create_instance_without_app_info() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    // pApplicationInfo deliberately null; no extensions, no layers.

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = lsw::vkproto::lsw_vkCreateInstance(t, &ci, nullptr, &instance);
    check(res == VK_SUCCESS, "minimal vkCreateInstance succeeded");
    check(s.create_instance_called, "minimal vkCreateInstance reached server");
    check(!s.ci_app_info_present, "minimal call had no VkApplicationInfo");
    check(s.ci_extensions.empty(), "no extensions");
    check(s.ci_layers.empty(), "no layers");
}

void test_create_device_with_two_queues_and_features() {
    RecordingServer s;
    lsw::vkproto::LoopbackTransport t{s};

    const float gfx_priorities[] = {1.0F, 0.5F};
    const float compute_priorities[] = {0.25F};

    VkDeviceQueueCreateInfo queues[2]{};
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = 0;
    queues[0].queueCount = 2;
    queues[0].pQueuePriorities = gfx_priorities;
    queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[1].queueFamilyIndex = 1;
    queues[1].queueCount = 1;
    queues[1].pQueuePriorities = compute_priorities;

    const char* exts[] = {"VK_KHR_swapchain"};

    VkPhysicalDeviceFeatures features{};
    features.geometryShader = VK_TRUE;
    features.tessellationShader = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 2;
    ci.pQueueCreateInfos = queues;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = exts;
    ci.pEnabledFeatures = &features;

    VkDevice dev = VK_NULL_HANDLE;
    VkResult res = lsw::vkproto::lsw_vkCreateDevice(t, kFakePhysDev0, &ci, nullptr, &dev);

    check(res == VK_SUCCESS, "vkCreateDevice returned success");
    check(dev == kFakeDevice, "device handle round-tripped");
    check(s.create_device_called, "server saw the call");
    check(s.cd_phys == kFakePhysDev0, "physical device round-tripped");
    check(s.cd_queue_families.size() == 2, "two queue create infos");
    check(s.cd_queue_families[0] == 0, "queue family 0");
    check(s.cd_queue_families[1] == 1, "queue family 1");
    check(s.cd_queue_priorities[0].size() == 2, "two priorities for q0");
    check(s.cd_queue_priorities[0][0] == 1.0F, "priority [0][0]");
    check(s.cd_queue_priorities[0][1] == 0.5F, "priority [0][1]");
    check(s.cd_queue_priorities[1].size() == 1, "one priority for q1");
    check(s.cd_queue_priorities[1][0] == 0.25F, "priority [1][0]");
    check(s.cd_extensions.size() == 1, "one device extension");
    check(s.cd_extensions[0] == "VK_KHR_swapchain", "ext name");
    check(s.cd_features_present, "pEnabledFeatures was forwarded");
    check(s.cd_features_geometry_shader == VK_TRUE, "VkBool32 inside features round-tripped");
}

}  // namespace

int main() {
    test_destroy_instance();
    test_destroy_device();
    test_get_device_queue();
    test_enumerate_query_then_fetch();
    test_enumerate_truncated_fetch();
    test_get_physical_device_properties2();
    test_create_instance_with_app_info_and_extensions();
    test_create_instance_without_app_info();
    test_create_device_with_two_queues_and_features();
    std::puts("OK");
    return 0;
}
