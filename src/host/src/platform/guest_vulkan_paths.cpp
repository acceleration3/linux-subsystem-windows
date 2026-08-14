#include "lsw/host/platform/guest_vulkan_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace lsw::host::platform {

namespace {

// Same shape as AgentExePath::locate: an override for developers, else
// the build tree relative to the manager binary.
[[nodiscard]] QString locate(const char* env_var, const QString& build_tree_relative) {
    const QString from_env = qEnvironmentVariable(env_var);
    if (!from_env.isEmpty() && QFile::exists(from_env)) {
        return from_env;
    }
    const QString candidate =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + build_tree_relative);
    return QFile::exists(candidate) ? candidate : QString{};
}

}  // namespace

QString GuestVulkanPaths::icd_dll() {
    return locate("LSW_ICD_DLL",
                  QStringLiteral("/../../../build-agent/remotevk/src/icd/lsw_icd.dll"));
}

QString GuestVulkanPaths::icd_dll_x86() {
    return locate("LSW_ICD_DLL_X86",
                  QStringLiteral("/../../../build-agent32/remotevk/src/icd/lsw_icd.dll"));
}

QString GuestVulkanPaths::vulkan_loader_x86() {
    return locate(
        "LSW_VULKAN_LOADER_X86",
        QStringLiteral("/../../../build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll"));
}

QString GuestVulkanPaths::vulkan_loader() {
    return locate(
        "LSW_VULKAN_LOADER",
        QStringLiteral("/../../../build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll"));
}

QString GuestVulkanPaths::icd_manifest_json() {
    // ``library_path`` is relative on purpose. The loader resolves a
    // relative path against the directory holding the manifest, so the
    // pair can be copied anywhere in the guest and still work — which
    // matters because the host cannot know where the guest's
    // ProgramData is, and hardcoding C:\ProgramData\LSW here would put
    // the same string in two places that must agree.
    return QStringLiteral(R"({
    "file_format_version": "1.0.1",
    "ICD": {
        "library_path": ".\\lsw_icd.dll",
        "api_version": "1.3.296"
    }
}
)");
}

}  // namespace lsw::host::platform
