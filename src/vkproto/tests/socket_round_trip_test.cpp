// Plumbing test: a real Unix-domain socket between an in-process server
// thread and a client thread. Verifies the full path:
//   generated client wrapper -> UnixSocketTransport ->
//   Server::serve_one -> Connection (reader+replay) ->
//   DispatchChain (HelloHandler + VulkanHandler) ->
//   generated server dispatch -> ServerDispatch impl ->
//   reply -> UnixSocketTransport -> application out-params.

#include "lsw/protocol/unix_endpoint.hpp"
#include "lsw/server/handler.hpp"
#include "lsw/server/handlers/vulkan_handler.hpp"
#include "lsw/server/server.hpp"
#include "vk_default_dispatch.h"
#include "vk_dispatch_client.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const auto kFakeInstance =
    reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0xAABBCCDDEEFF0011ULL));
const auto kFakeDevice =
    reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(0x1122334455667788ULL));
const auto kFakePhysDev =
    reinterpret_cast<VkPhysicalDevice>(static_cast<std::uintptr_t>(0x33445566ULL));

struct EchoServer final : lsw::vkproto::DefaultServerDispatch {
    std::string seen_app_name;
    std::uint32_t seen_api_version = 0;
    std::vector<std::string> seen_extensions;
    bool destroyed = false;

    VkResult on_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks*, VkInstance* pInstance) override {
        if (pCreateInfo->pApplicationInfo != nullptr) {
            const auto& app = *pCreateInfo->pApplicationInfo;
            if (app.pApplicationName != nullptr) {
                seen_app_name = app.pApplicationName;
            }
            seen_api_version = app.apiVersion;
        }
        for (std::uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            seen_extensions.emplace_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }
        *pInstance = kFakeInstance;
        return VK_SUCCESS;
    }

    void on_vkDestroyInstance(VkInstance, const VkAllocationCallbacks*) override {
        destroyed = true;
    }

    VkResult on_vkEnumeratePhysicalDevices(VkInstance, std::uint32_t* pCount,
                                           VkPhysicalDevice* pDevices) override {
        if (pDevices == nullptr) {
            *pCount = 1;
            return VK_SUCCESS;
        }
        if (*pCount >= 1) {
            pDevices[0] = kFakePhysDev;
            *pCount = 1;
        }
        return VK_SUCCESS;
    }

    VkResult on_vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*,
                               const VkAllocationCallbacks*, VkDevice* pDevice) override {
        *pDevice = kFakeDevice;
        return VK_SUCCESS;
    }

    void on_vkDestroyDevice(VkDevice, const VkAllocationCallbacks*) override {}

    void on_vkGetDeviceQueue(VkDevice, std::uint32_t, std::uint32_t, VkQueue* pQueue) override {
        *pQueue = reinterpret_cast<VkQueue>(static_cast<std::uintptr_t>(0xDEADBEEFULL));
    }

    void on_vkGetPhysicalDeviceProperties2(VkPhysicalDevice,
                                           VkPhysicalDeviceProperties2* pProps) override {
        pProps->properties.apiVersion = VK_API_VERSION_1_3;
        pProps->properties.vendorID = 0xCAFE;
    }
};

void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

std::string make_socket_path() {
    auto p = std::filesystem::temp_directory_path() /
             ("lsw-test-" + std::to_string(::getpid()) + ".sock");
    return p.string();
}

}  // namespace

int main() {
    const std::string sock_path = make_socket_path();

    EchoServer echo;
    auto build_chain = [&](::lsw::server::Session& /*s*/, ::lsw::server::DispatchChain& chain) {
        // No HelloHandler: the test sends Vulkan calls directly without HELLO,
        // matching the original test's narrow scope.
        chain.add(std::make_unique<::lsw::server::handlers::VulkanHandler>(echo));
    };

    lsw::protocol::UnixListener listener{sock_path};
    check(listener.bind() == 0, "server bind succeeded");

    std::atomic<bool> server_done{false};
    std::thread server_thread{[&] {
        ::lsw::server::Server server{listener, build_chain};
        server.serve_one();
        server_done.store(true, std::memory_order_release);
    }};

    // Give the listener a moment to be ready before we connect. Spurious
    // ECONNREFUSED happens if we race the listen() call.
    bool client_ran = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        lsw::protocol::UnixSocketTransport t;
        if (t.connect(sock_path) == 0) {
            client_ran = true;
            // ---- vkCreateInstance round-trip over the real socket ----
            VkApplicationInfo app{};
            app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app.pApplicationName = "lsw-socket-test";
            app.apiVersion = VK_API_VERSION_1_3;

            const char* exts[] = {"VK_KHR_surface"};

            VkInstanceCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            ci.pApplicationInfo = &app;
            ci.enabledExtensionCount = 1;
            ci.ppEnabledExtensionNames = exts;

            VkInstance instance = VK_NULL_HANDLE;
            VkResult res = lsw::vkproto::lsw_vkCreateInstance(t, &ci, nullptr, &instance);
            check(res == VK_SUCCESS, "vkCreateInstance via socket succeeded");
            check(instance == kFakeInstance, "instance handle round-tripped over socket");

            // Two-call enumerate over the same socket connection.
            std::uint32_t count = 0;
            res = lsw::vkproto::lsw_vkEnumeratePhysicalDevices(t, instance, &count, nullptr);
            check(res == VK_SUCCESS, "enumerate query succeeded");
            check(count == 1, "server reported 1 device");

            std::vector<VkPhysicalDevice> devs(count, VK_NULL_HANDLE);
            res = lsw::vkproto::lsw_vkEnumeratePhysicalDevices(t, instance, &count, devs.data());
            check(res == VK_SUCCESS, "enumerate fetch succeeded");
            check(devs[0] == kFakePhysDev, "phys device round-tripped over socket");

            // Async path: vkDestroyInstance.
            lsw::vkproto::lsw_vkDestroyInstance(t, instance, nullptr);

            // Closing the transport will EOF the server read loop.
            t.close();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(client_ran, "client connected");

    server_thread.join();
    check(server_done.load(std::memory_order_acquire), "server loop terminated");
    check(echo.seen_app_name == "lsw-socket-test",
          "application name reached server over real socket");
    check(echo.seen_api_version == VK_API_VERSION_1_3, "apiVersion reached server");
    check(echo.seen_extensions.size() == 1, "1 extension reached server");
    check(echo.seen_extensions[0] == "VK_KHR_surface", "extension name reached server");
    check(echo.destroyed, "vkDestroyInstance reached server (async)");

    std::puts("OK");
    return 0;
}
