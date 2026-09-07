#include "lsw/host/platform/guest_driver_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <utility>

namespace lsw::host::platform {

GuestDriverPaths::GuestDriverPaths(QString build_tree, QString env_prefix)
    : build_tree_(std::move(build_tree)), env_prefix_(std::move(env_prefix)) {}

GuestDriverPaths GuestDriverPaths::x86_64() {
    return {QStringLiteral("/../../../build-agent"), QStringLiteral("LSW")};
}

GuestDriverPaths GuestDriverPaths::i686() {
    return {QStringLiteral("/../../../build-agent32"), QStringLiteral("LSW")};
}

QString GuestDriverPaths::locate(const char* name, const char* relative) const {
    // The i686 overrides keep their _X86 suffix: they name a specific
    // file, and a developer setting one is saying which build of that
    // architecture to use.
    const QString variable = env_prefix_ + QLatin1Char('_') + QLatin1String(name)
                             + (build_tree_.endsWith(QLatin1String("32"))
                                    ? QStringLiteral("_X86")
                                    : QString{});
    QString from_env = qEnvironmentVariable(variable.toLatin1().constData());
    if (!from_env.isEmpty() && QFile::exists(from_env)) {
        return from_env;
    }
    const QString candidate = QDir::cleanPath(QCoreApplication::applicationDirPath()
                                              + build_tree_ + QLatin1String(relative));
    return QFile::exists(candidate) ? candidate : QString{};
}

QString GuestDriverPaths::vulkan_icd() const {
    return locate("VULKAN_ICD_DLL", "/render-server/src/apis/vulkan/icd/librvk_icd.dll");
}

QString GuestDriverPaths::opengl_icd() const {
    return locate("OPENGL_ICD_DLL", "/render-server/src/apis/opengl/icd/opengl32.dll");
}

QString GuestDriverPaths::opencl_icd() const {
    return locate("OPENCL_ICD_DLL", "/render-server/src/apis/opencl/icd/librvk_opencl.dll");
}

QString GuestDriverPaths::vulkan_loader() const {
    return locate("VULKAN_LOADER", "/_deps/vulkanloader-build/loader/vulkan-1.dll");
}

}  // namespace lsw::host::platform
