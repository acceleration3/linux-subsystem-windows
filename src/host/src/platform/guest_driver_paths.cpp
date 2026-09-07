#include "lsw/host/platform/guest_driver_paths.hpp"

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

// The two cross-build trees scripts/build-agent.sh writes, relative to
// the manager binary.
constexpr auto kBuild64 = "/../../../build-agent";
constexpr auto kBuild32 = "/../../../build-agent32";

[[nodiscard]] QString in_build(const char* tree, const char* relative) {
    return QString::fromLatin1(tree) + QLatin1String(relative);
}

}  // namespace

QString GuestDriverPaths::vulkan_icd() {
    return locate("LSW_VULKAN_ICD_DLL",
                  in_build(kBuild64, "/render-server/src/apis/vulkan/icd/librvk_icd.dll"));
}

QString GuestDriverPaths::opengl_icd() {
    return locate("LSW_OPENGL_ICD_DLL",
                  in_build(kBuild64, "/render-server/src/apis/opengl/icd/opengl32.dll"));
}

QString GuestDriverPaths::opencl_icd() {
    return locate("LSW_OPENCL_ICD_DLL",
                  in_build(kBuild64, "/render-server/src/apis/opencl/icd/librvk_opencl.dll"));
}

QString GuestDriverPaths::vulkan_loader() {
    return locate("LSW_VULKAN_LOADER",
                  in_build(kBuild64, "/_deps/vulkanloader-build/loader/vulkan-1.dll"));
}

QString GuestDriverPaths::vulkan_icd_x86() {
    return locate("LSW_VULKAN_ICD_DLL_X86",
                  in_build(kBuild32, "/render-server/src/apis/vulkan/icd/librvk_icd.dll"));
}

QString GuestDriverPaths::opengl_icd_x86() {
    return locate("LSW_OPENGL_ICD_DLL_X86",
                  in_build(kBuild32, "/render-server/src/apis/opengl/icd/opengl32.dll"));
}

QString GuestDriverPaths::opencl_icd_x86() {
    return locate("LSW_OPENCL_ICD_DLL_X86",
                  in_build(kBuild32, "/render-server/src/apis/opencl/icd/librvk_opencl.dll"));
}

QString GuestDriverPaths::vulkan_loader_x86() {
    return locate("LSW_VULKAN_LOADER_X86",
                  in_build(kBuild32, "/_deps/vulkanloader-build/loader/vulkan-1.dll"));
}

}  // namespace lsw::host::platform
