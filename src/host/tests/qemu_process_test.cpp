#include "lsw/host/platform/qemu_process.hpp"

#include <QCoreApplication>
#include <cstdio>

using lsw::host::platform::QemuProcess;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

QByteArray cmdline(std::initializer_list<const char*> args) {
    QByteArray out;
    for (const char* arg : args) {
        out.append(arg);
        out.append('\0');
    }
    return out;
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    const QByteArray uuid = "6f1c2a3e-0000-4000-8000-123456789abc";

    check(QemuProcess::runs_domain(cmdline({"qemu-system-x86_64", "-uuid", "6f1c2a3e-0000-4000-8000-123456789abc", "-m", "4G"}), uuid),
          "-uuid followed by the uuid as a separate argument matches");
    check(QemuProcess::runs_domain(cmdline({"qemu", "-uuid", "6F1C2A3E-0000-4000-8000-123456789ABC"}), uuid),
          "the uuid compares case-insensitively");
    check(!QemuProcess::runs_domain(cmdline({"qemu", "-uuid", "6f1c2a3e-0000-4000-8000-123456789abd"}), uuid),
          "another uuid does not match");
    check(!QemuProcess::runs_domain(cmdline({"qemu", "-name", "6f1c2a3e-0000-4000-8000-123456789abc"}), uuid),
          "the uuid after another option does not match");
    check(!QemuProcess::runs_domain(cmdline({"qemu", "-uuid=6f1c2a3e-0000-4000-8000-123456789abc"}), uuid),
          "a joined -uuid=value argument does not match");
    check(!QemuProcess::runs_domain(cmdline({"qemu", "-uuid 6f1c2a3e-0000-4000-8000-123456789abc"}), uuid),
          "-uuid and the uuid in one argument do not match");
    check(!QemuProcess::runs_domain(cmdline({"qemu", "-uuid"}), uuid), "a trailing -uuid does not match");
    check(!QemuProcess::runs_domain(cmdline({"qemu", "-uuid", "6f1c2a3e-0000-4000-8000-123456789abc-extra"}), uuid),
          "a longer argument does not match");
    check(!QemuProcess::runs_domain(QByteArray{}, uuid), "an empty command line does not match");
    check(QemuProcess::runs_domain(QByteArray("-uuid\0" "6f1c2a3e-0000-4000-8000-123456789abc", 42), uuid),
          "the last argument needs no terminator");
    check(QemuProcess::pid_of(QUuid{}) == -1, "a null uuid has no process");
    check(QemuProcess::pid_of(QUuid::createUuid()) == -1, "an unknown uuid has no process");

    return g_failures == 0 ? 0 : 1;
}
