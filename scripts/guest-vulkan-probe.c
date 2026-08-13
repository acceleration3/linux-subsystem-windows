// Smallest thing that proves the guest Vulkan path is real.
//
// Loads vulkan-1.dll dynamically (no import library, so it builds with
// nothing but a compiler and the headers), creates an instance,
// enumerates physical devices and creates a logical device. Every one of
// those calls travels: application -> Khronos loader -> lsw_icd.dll ->
// IVSHMEM cmdstream ring -> host render server -> real GPU, and back.
//
// It draws nothing on purpose. A window would drag in swapchain and
// surface handling, and the question this answers first is narrower and
// has to be answered before any of that matters: does a guest process
// reach the host's GPU at all?
//
// Build: see scripts/guest-vulkan-probe.sh

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <windows.h>

int main(void) {
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) {
        printf("FAIL: vulkan-1.dll did not load (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("ok   vulkan-1.dll loaded\n");

    PFN_vkGetInstanceProcAddr gipa =
        (PFN_vkGetInstanceProcAddr)(void*)GetProcAddress(lib, "vkGetInstanceProcAddr");
    if (!gipa) {
        printf("FAIL: no vkGetInstanceProcAddr\n");
        return 1;
    }

    PFN_vkCreateInstance create_instance =
        (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    if (!create_instance) {
        printf("FAIL: no vkCreateInstance\n");
        return 1;
    }

    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "lsw-guest-probe";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult rc = create_instance(&ici, NULL, &instance);
    if (rc != VK_SUCCESS) {
        // The interesting failure. VK_ERROR_INCOMPATIBLE_DRIVER means the
        // loader found no usable ICD: either the registry value is
        // missing, or lsw_icd.dll failed to load, or it could not reach
        // the host.
        printf("FAIL: vkCreateInstance -> %d\n", (int)rc);
        return 1;
    }
    printf("ok   vkCreateInstance (the ICD answered over shared memory)\n");

    PFN_vkEnumeratePhysicalDevices enum_devs =
        (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties get_props =
        (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");

    uint32_t count = 0;
    rc = enum_devs(instance, &count, NULL);
    printf("ok   vkEnumeratePhysicalDevices -> %u device(s)\n", count);
    if (rc != VK_SUCCESS || count == 0) {
        printf("FAIL: no physical devices\n");
        return 1;
    }
    if (count > 8) count = 8;
    VkPhysicalDevice devs[8];
    enum_devs(instance, &count, devs);

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p = {0};
        get_props(devs[i], &p);
        // This name comes from the HOST's driver. Seeing it inside the
        // guest is the whole point.
        printf("ok   device[%u]: %s (api %u.%u.%u)\n", i, p.deviceName,
               VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
               VK_API_VERSION_PATCH(p.apiVersion));
    }

    PFN_vkCreateDevice create_device = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
    float prio = 1.0f;
    VkDeviceQueueCreateInfo q = {0};
    q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q.queueFamilyIndex = 0;
    q.queueCount = 1;
    q.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;

    VkDevice device = VK_NULL_HANDLE;
    rc = create_device(devs[0], &dci, NULL, &device);
    if (rc != VK_SUCCESS) {
        printf("FAIL: vkCreateDevice -> %d\n", (int)rc);
        return 1;
    }
    printf("ok   vkCreateDevice\n");

    PFN_vkDestroyDevice destroy_device = (PFN_vkDestroyDevice)gipa(instance, "vkDestroyDevice");
    PFN_vkDestroyInstance destroy_instance =
        (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
    destroy_device(device, NULL);
    destroy_instance(instance, NULL);
    printf("PASS: the guest reached the host GPU through LSW\n");
    return 0;
}
