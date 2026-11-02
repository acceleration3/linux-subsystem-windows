#include "lsw/host/platform/mount_table.hpp"

#include <cstdio>

using lsw::host::platform::MountTable;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}  // namespace

int main() {
    const QByteArray sample =
        "22 28 0:21 / /sys rw,nosuid,nodev,noexec,relatime shared:7 - sysfs sysfs rw\n"
        "36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,errors=continue\n"
        "40 28 0:40 / /home/user/Windows rw,relatime - fuse.lsw-windows-fs //127.0.0.1/C$ "
        "rw,vers=3.1.1,uid=1000,gid=1000\n"
        "41 28 0:41 / /home/user/My\\040Files rw,relatime shared:9 master:3 - fuse.lsw-windows-fs "
        "//127.0.0.1/Users rw,uid=1000\n"
        "42 28 0:42 / /srv/other rw,relatime - fuse.lsw-windows-fs //192.168.1.5/share rw\n"
        "43 28 0:43 / /home/user/Remote rw,relatime - fuse.lsw-windows-fs //127.0.0.1/D$ rw\n"
        "44 28 0:44 / /home/user/Windows rw,relatime - fuse.lsw-windows-fs //127.0.0.1/Users "
        "rw,uid=1000\n"
        "garbage line\n"
        "45 28 0:45 / /broken rw\n"
        "\n";
    const MountTable table = MountTable::parse(sample);

    check(table.entries().size() == 7, "seven well-formed lines are read, the broken ones skipped");
    const auto* ext = table.find(QStringLiteral("/mnt2"));
    check(ext != nullptr && ext->fstype == QStringLiteral("ext3") &&
              ext->source == QStringLiteral("/dev/root") &&
              ext->options == QStringLiteral("rw,errors=continue"),
          "fields after the optional ones are read: type, source, options");
    const auto* sys = table.find(QStringLiteral("/sys"));
    check(sys != nullptr && sys->fstype == QStringLiteral("sysfs"),
          "a line with one optional field parses");
    const auto* spaced = table.find(QStringLiteral("/home/user/My Files"));
    check(spaced != nullptr && spaced->source == QStringLiteral("//127.0.0.1/Users"),
          "an octal escaped space in the mount point is decoded, with two optional fields");
    const auto* top = table.find(QStringLiteral("/home/user/Windows"));
    check(top != nullptr && top->source == QStringLiteral("//127.0.0.1/Users"),
          "find returns the topmost of stacked mounts");
    check(table.find(QStringLiteral("/nowhere")) == nullptr, "an unknown path is not found");

    const auto guest = table.guest_shares();
    check(guest.size() == 3,
          "only the guest filesystem's mounts of the guest's C$ and Users count");
    bool other = false;
    bool disk_d = false;
    for (const auto& entry : guest) {
        other = other || entry.mount_point == QStringLiteral("/srv/other");
        disk_d = disk_d || entry.mount_point == QStringLiteral("/home/user/Remote");
    }
    check(!other && !disk_d, "another host's share and the guest's D$ are left alone");

    check(MountTable::parse({}).entries().isEmpty(), "empty input is an empty table");
    check(MountTable::read(QStringLiteral("/nonexistent/mountinfo")).entries().isEmpty(),
          "an unreadable file is an empty table");
    check(!MountTable::read().entries().isEmpty(), "the real /proc/self/mountinfo has entries");

    return g_failures == 0 ? 0 : 1;
}
