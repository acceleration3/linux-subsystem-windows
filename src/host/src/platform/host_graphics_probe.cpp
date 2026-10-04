#include "lsw/host/platform/host_graphics_probe.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>

namespace lsw::host::platform {

namespace {

// The little of EGL the probe needs, declared here rather than taken
// from <EGL/egl.h>: libEGL is opened at run time, so the manager neither
// links it nor needs its headers to build.
using EGLDisplay = void*;
using EGLBoolean = unsigned int;
using EGLint = std::int32_t;
using EGLenum = unsigned int;
constexpr EGLint kEglVersion = 0x3054;
constexpr EGLint kEglClientApis = 0x308D;
constexpr EGLenum kEglOpenGlApi = 0x30A2;
constexpr EGLenum kEglPlatformSurfacelessMesa = 0x31DD;
using GetProcAddress = void* (*)(const char*);
using GetPlatformDisplay = EGLDisplay (*)(EGLenum, void*, const std::intptr_t*);
using GetDisplay = EGLDisplay (*)(void*);
using Initialize = EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*);
using Terminate = EGLBoolean (*)(EGLDisplay);
using QueryString = const char* (*)(EGLDisplay, EGLint);
using BindApi = EGLBoolean (*)(EGLenum);
using GetDisplayDriverName = const char* (*)(EGLDisplay);

// What the library a Khronos OpenCL ICD file names is usually called.
QString icd_label(const QString& library) {
    const QString lower = library.toLower();
    if (lower.contains(QLatin1StringView("rusticl"))) return QStringLiteral("rusticl");
    if (lower.contains(QLatin1StringView("nvidia"))) return QStringLiteral("NVIDIA");
    if (lower.contains(QLatin1StringView("amdocl"))) return QStringLiteral("ROCm");
    if (lower.contains(QLatin1StringView("pocl"))) return QStringLiteral("PoCL");
    if (lower.contains(QLatin1StringView("igdrcl"))) return QStringLiteral("Intel Compute Runtime");
    if (lower.contains(QLatin1StringView("clvk"))) return QStringLiteral("clvk");
    QString base = QFileInfo(library).fileName();
    base.remove(QRegularExpression(QStringLiteral("\\.so.*$")));
    return base;
}

// The first file in ``dir`` matching ``pattern``; the one with the
// longest name, so libdxvk_d3d11.so.0.20701 wins over its symlinks.
QString find_library(const QString& dir, const QString& pattern) {
    if (dir.isEmpty()) {
        return {};
    }
    QStringList found = QDir(dir).entryList({pattern}, QDir::Files | QDir::System);
    if (found.isEmpty()) {
        return {};
    }
    std::sort(found.begin(), found.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });
    return found.first();
}

// The version a native DXVK build encodes in its soname,
// "libdxvk_d3d11.so.0.20701" being 2.7.1. Empty when the name carries
// none.
QString soname_version(const QString& file) {
    static const QRegularExpression pattern(QStringLiteral("\\.so\\.\\d+\\.(\\d{5,})$"));
    const auto match = pattern.match(file);
    if (!match.hasMatch()) {
        return {};
    }
    const int encoded = match.captured(1).toInt();
    return QStringLiteral("%1.%2.%3")
        .arg(encoded / 10000)
        .arg((encoded / 100) % 100)
        .arg(encoded % 100);
}

}  // namespace

QString HostGraphicsProbe::driver_name(const HostGpu& gpu) {
    const QString driver = gpu.driver.toLower();
    if (driver == QLatin1StringView("radv")) return QStringLiteral("RADV");
    if (driver.contains(QLatin1StringView("intel open-source mesa"))) return QStringLiteral("ANV");
    if (driver == QLatin1StringView("nvk") || driver.contains(QLatin1StringView("nouveau")))
        return QStringLiteral("NVK");
    if (driver.contains(QLatin1StringView("nvidia"))) return QStringLiteral("NVIDIA");
    if (driver.contains(QLatin1StringView("amd proprietary")) ||
        driver.contains(QLatin1StringView("amdvlk")) ||
        driver.contains(QLatin1StringView("amd open-source")))
        return QStringLiteral("AMDVLK");
    if (driver == QLatin1StringView("llvmpipe")) return QStringLiteral("llvmpipe");
    if (driver.contains(QLatin1StringView("turnip"))) return QStringLiteral("Turnip");
    if (driver.contains(QLatin1StringView("venus"))) return QStringLiteral("Venus");
    return gpu.driver.isEmpty() ? gpu.name : gpu.driver;
}

QString HostGraphicsProbe::driver_label(const HostGpu& gpu) {
    const QString name = driver_name(gpu);
    // driverInfo is a version for most drivers ("Mesa 25.2.3", "580.82")
    // and a sentence for some; only a short one is worth the room.
    QString info = gpu.driver_info.trimmed();
    // A distribution's build suffix, "Mesa 26.2.4-arch3.1", is not the
    // version anyone looks up.
    static const QRegularExpression suffix(QStringLiteral("^(Mesa \\d+\\.\\d+(\\.\\d+)?)[-+].*$"));
    info.replace(suffix, QStringLiteral("\\1"));
    if (info.size() > 24) {
        info.clear();
    }
    if (name == QLatin1StringView("llvmpipe") || info.isEmpty()) {
        return name;
    }
    return QStringLiteral("%1, %2").arg(name, info);
}

HostApi HostGraphicsProbe::vulkan(const QVector<HostGpu>& gpus) {
    HostApi api;
    api.kind = HostApi::Kind::Vulkan;
    api.name = QStringLiteral("Vulkan");
    QStringList drivers;
    for (const auto& gpu : gpus) {
        const QString name = driver_name(gpu);
        if (!drivers.contains(name)) {
            drivers << name;
        }
    }
    api.served = !drivers.isEmpty();
    if (!api.served) {
        api.runs_on = QStringLiteral("no Vulkan driver on this host");
    } else if (drivers.size() == 1) {
        api.runs_on = QStringLiteral("%1, the host Vulkan driver").arg(drivers.first());
    } else {
        const QString last = drivers.takeLast();
        api.runs_on = QStringLiteral("%1 and %2, the host Vulkan drivers")
                          .arg(drivers.join(QStringLiteral(", ")), last);
    }
    return api;
}

HostApi HostGraphicsProbe::opengl() {
    HostApi api;
    api.kind = HostApi::Kind::OpenGl;
    api.name = QStringLiteral("OpenGL");
    api.runs_on = QStringLiteral("no EGL with desktop OpenGL");

    void* library = ::dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        api.runs_on = QStringLiteral("libEGL.so.1 not found");
        return api;
    }
    const auto symbol = [library](const char* name) { return ::dlsym(library, name); };
    auto* get_proc = reinterpret_cast<GetProcAddress>(symbol("eglGetProcAddress"));
    auto* initialize = reinterpret_cast<Initialize>(symbol("eglInitialize"));
    auto* terminate = reinterpret_cast<Terminate>(symbol("eglTerminate"));
    auto* query = reinterpret_cast<QueryString>(symbol("eglQueryString"));
    auto* bind = reinterpret_cast<BindApi>(symbol("eglBindAPI"));
    if (get_proc != nullptr && initialize != nullptr && terminate != nullptr && query != nullptr
        && bind != nullptr) {
        // The executor's own choice: a display with no window system,
        // since it draws for the guest and never on a host window.
        // Mesa's surfaceless platform where there is one, the default
        // display otherwise.
        EGLDisplay display = nullptr;
        if (auto* platform =
                reinterpret_cast<GetPlatformDisplay>(get_proc("eglGetPlatformDisplay"))) {
            display = platform(kEglPlatformSurfacelessMesa, nullptr, nullptr);
        }
        if (display == nullptr) {
            if (auto* fallback = reinterpret_cast<GetDisplay>(symbol("eglGetDisplay"))) {
                display = fallback(nullptr);
            }
        }
        EGLint major = 0;
        EGLint minor = 0;
        if (display != nullptr && initialize(display, &major, &minor) != 0) {
            const QString apis = QString::fromLatin1(query(display, kEglClientApis));
            if (apis.contains(QLatin1StringView("OpenGL")) && bind(kEglOpenGlApi) != 0) {
                api.served = true;
                QString driver;
                if (auto* driver_of = reinterpret_cast<GetDisplayDriverName>(
                        get_proc("eglGetDisplayDriverName"))) {
                    driver = QString::fromLatin1(driver_of(display));
                }
                api.runs_on = driver.isEmpty()
                                  ? QStringLiteral("EGL %1.%2").arg(major).arg(minor)
                                  : QStringLiteral("EGL, %1").arg(driver);
            }
            terminate(display);
        }
    }
    // Left loaded: unloading a GL driver some other library may have
    // pulled in is how a process crashes at exit.
    return api;
}

HostApi HostGraphicsProbe::opencl() {
    HostApi api;
    api.kind = HostApi::Kind::OpenCl;
    api.name = QStringLiteral("OpenCL");
    // The Khronos loader's own list: one file per ICD, holding the
    // library's name. OCL_ICD_VENDORS moves it.
    QString vendors = qEnvironmentVariable("OCL_ICD_VENDORS");
    if (vendors.isEmpty() || !QFileInfo(vendors).isDir()) {
        vendors = QStringLiteral("/etc/OpenCL/vendors");
    }
    QStringList names;
    for (const QFileInfo& icd : QDir(vendors).entryInfoList({QStringLiteral("*.icd")}, QDir::Files)) {
        QFile file(icd.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString label = icd_label(QString::fromUtf8(file.readAll()).trimmed());
        if (!label.isEmpty() && !names.contains(label)) {
            names << label;
        }
    }
    api.served = !names.isEmpty();
    api.runs_on = api.served ? names.join(QStringLiteral(", "))
                             : QStringLiteral("no OpenCL ICD in %1").arg(vendors);
    return api;
}

HostApi HostGraphicsProbe::direct3d(HostApi::Kind kind, const QString& d3d_dir) {
    const bool eleven = kind == HostApi::Kind::Direct3d11;
    HostApi api;
    api.kind = kind;
    api.required = true;
    api.name = eleven ? QStringLiteral("Direct3D 11") : QStringLiteral("Direct3D 12");
    const QString project = eleven ? QStringLiteral("DXVK") : QStringLiteral("vkd3d-proton");
    const QString file = find_library(d3d_dir, eleven ? QStringLiteral("libdxvk_d3d11.so*")
                                                      : QStringLiteral("libvkd3d-proton-d3d12.so*"));
    api.served = !file.isEmpty();
    if (!api.served) {
        api.runs_on = d3d_dir.isEmpty() ? QStringLiteral("%1 not found").arg(project)
                                        : QStringLiteral("%1 not in %2").arg(project, d3d_dir);
        return api;
    }
    const QString version = soname_version(file);
    api.runs_on = version.isEmpty() ? QStringLiteral("%1, native").arg(project)
                                    : QStringLiteral("%1 %2, native").arg(project, version);
    return api;
}

QVector<HostApi> HostGraphicsProbe::probe(const QVector<HostGpu>& gpus, const QString& d3d_dir) {
    return {
        vulkan(gpus),
        opengl(),
        opencl(),
        direct3d(HostApi::Kind::Direct3d11, d3d_dir),
        direct3d(HostApi::Kind::Direct3d12, d3d_dir),
    };
}

bool HostGraphicsProbe::missing_required(const QVector<HostApi>& apis) {
    return std::any_of(apis.begin(), apis.end(),
                       [](const HostApi& api) { return api.required && !api.served; });
}

}  // namespace lsw::host::platform
