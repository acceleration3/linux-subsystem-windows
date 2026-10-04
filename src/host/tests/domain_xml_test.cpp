// The domain definition is built from strings a user chooses — shared
// folder paths above all — and the template quotes its attributes with
// '. A folder called "Tom's & Jerry's" used to go into the XML as it
// was, which is a definition libvirt refuses, with an error that names
// a line of generated XML rather than the folder.
//
// This pins the escaping, the single-pass substitution that keeps a
// substituted value from being substituted again, and the settings
// half of the definition (DomainXmlInputs from ManagerSettings). No
// libvirt, no VM.

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/services/domain_inputs.hpp"
#include "lsw/host/vm/domain_xml_builder.hpp"

#include <QCoreApplication>
#include <QDomDocument>
#include <QHash>
#include <QUuid>
#include <cstdio>
#include <cstdlib>

using lsw::host::config::HostShare;
using lsw::host::config::ManagerSettings;
using lsw::host::config::VmRecord;
using lsw::host::vm::DomainShare;
using lsw::host::vm::DomainXmlBuilder;
using lsw::host::vm::DomainXmlInputs;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    check(DomainXmlBuilder::xml_escape(QStringLiteral("a&b<c>d\"e'f")) ==
              QStringLiteral("a&amp;b&lt;c&gt;d&quot;e&apos;f"),
          "xml_escape covers all five XML specials");
    check(DomainXmlBuilder::xml_escape(QStringLiteral("/home/ünïcode/plain")) ==
              QStringLiteral("/home/ünïcode/plain"),
          "xml_escape leaves ordinary text alone");

    // One pass: a value that looks like a placeholder is not expanded.
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("a"), QStringLiteral("{{b}}"));
    vars.insert(QStringLiteral("b"), QStringLiteral("B"));
    check(DomainXmlBuilder::render_template(QStringLiteral("<{{a}}|{{b}}|{{c}}>"), vars) ==
              QStringLiteral("<{{b}}|B|{{c}}>"),
          "render_template substitutes once and leaves unknown placeholders");

    // End to end: a hostile share path and a hostile disk path survive
    // into a definition that parses and reads back as what was asked.
    VmRecord vm;
    vm.uuid = QUuid::createUuid();
    vm.name = QStringLiteral("lsw");
    vm.vsock_cid = 100;
    vm.disk_path = QStringLiteral("/home/o'brien/disks/lsw.qcow2");
    vm.nvram_path = QStringLiteral("/tmp/nvram.fd");
    vm.iso_path = QStringLiteral("/tmp/windows.iso");
    vm.payload_iso_path = QStringLiteral("/tmp/payload.iso");
    DomainXmlInputs inputs;
    inputs.virtio_iso = QStringLiteral("/tmp/virtio-win.iso");
    inputs.ivshmem_socket = QStringLiteral("/tmp/ivshmem.sock");
    const QString hostile = QStringLiteral("/home/me/Tom's & Jerry's <stuff>");
    inputs.shares.push_back(DomainShare{QStringLiteral("z"), hostile, true});

    const QString xml = DomainXmlBuilder::build(vm, inputs);
    QDomDocument doc;
    const QDomDocument::ParseResult parsed = doc.setContent(xml);
    if (!parsed) {
        std::printf("       line %lld: %s\n", static_cast<long long>(parsed.errorLine),
                    qPrintable(parsed.errorMessage));
    }
    check(static_cast<bool>(parsed), "a definition with hostile paths is well-formed XML");

    const QDomElement fs = doc.elementsByTagName(QStringLiteral("filesystem")).at(0).toElement();
    check(fs.firstChildElement(QStringLiteral("source")).attribute(QStringLiteral("dir")) ==
              hostile,
          "the share path reads back exactly");
    check(!fs.firstChildElement(QStringLiteral("readonly")).isNull(),
          "a read-only share is marked readonly");

    bool disk_found = false;
    const QDomNodeList sources = doc.elementsByTagName(QStringLiteral("source"));
    for (int i = 0; i < sources.count(); ++i) {
        if (sources.at(i).toElement().attribute(QStringLiteral("file")) == vm.disk_path) {
            disk_found = true;
        }
    }
    check(disk_found, "the disk path with an apostrophe reads back exactly");
    check(!xml.contains(QStringLiteral("{{")), "no placeholder is left unsubstituted");

    // The settings half. Video memory follows the mode.
    check(lsw::host::services::video_vram_kib_for(640, 480) == 16384,
          "small modes get the 16 MiB floor");
    check(lsw::host::services::video_vram_kib_for(3840, 2160) == (3840 * 2160 * 8 / 1024) + 1024,
          "a 4K mode gets two 32-bit buffers plus slack");

    // The stored form of the share list round-trips, read-only flag
    // and all, and an empty list is the empty string.
    const QVector<HostShare> shares = {
        HostShare{QStringLiteral("/home/me/a b"), QStringLiteral("Z:"), false},
        HostShare{QStringLiteral("/srv/ro"), QStringLiteral("Y:"), true},
    };
    const QString blob = ManagerSettings::encode_host_shares(shares);
    check(ManagerSettings::decode_host_shares(blob) == shares, "host shares round-trip");
    check(ManagerSettings::encode_host_shares({}).isEmpty(), "no shares encode as empty");
    check(ManagerSettings::decode_host_shares(QString{}).isEmpty(), "empty decodes as no shares");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
