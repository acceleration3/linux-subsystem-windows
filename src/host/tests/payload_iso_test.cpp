// What the payload ISO carries of a release (PayloadIsoBuilder::stage_files):
// each bundle file as a real file in real directories under update/, the
// way an update stages it, plus the manifest and the mount list, byte for
// byte the bundle's.

#include "lsw/host/provision/install/payload_inputs.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>

using lsw::host::provision::install::PayloadInputs;
using lsw::host::provision::install::PayloadIsoBuilder;
using lsw::host::services::GuestBundle;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

QByteArray read(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{"<unreadable>"};
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    auto bundle = std::make_shared<GuestBundle::Contents>();
    const auto add = [&](const char* path, const QByteArray& bytes) {
        bundle->files.push_back({path, bytes, "hash"});
    };
    add("C:\\ProgramData\\LSW\\lsw-agent.exe", QByteArray("agent\0bytes", 11));
    add("C:\\av\\package\\avgpu.sys", QByteArray(70000, '\x7f'));
    add("C:\\av\\loaders\\x64\\vulkan-1.dll", "vk");
    add("C:\\av\\provision.ps1", "ps");
    bundle->manifest = "m1  C:\\a\nm2  C:\\b\n";
    bundle->shares = "d=D:=SG9zdA==";
    bundle->release = "r";

    PayloadInputs inputs;
    inputs.autounattend_xml = QStringLiteral("<unattend/>");
    inputs.bundle = bundle;

    QTemporaryDir staging;
    QStringList created;
    QString error;
    check(PayloadIsoBuilder::stage_files(inputs, staging.path(), &created, &error), "stage_files succeeds with a bundle");
    const QDir update{QDir(staging.path()).filePath(QStringLiteral("update"))};

    const auto at = [&](const char* relative) { return update.filePath(QString::fromLatin1(relative)); };
    check(QFileInfo(at("C/ProgramData/LSW")).isDir() && QFileInfo(at("C/av/package")).isDir()
              && QFileInfo(at("C/av/loaders/x64")).isDir(),
          "the directories of a guest path are real directories under update/<drive>");
    check(QFileInfo(at("C/ProgramData/LSW/lsw-agent.exe")).isFile() && QFileInfo(at("C/av/package/avgpu.sys")).isFile()
              && QFileInfo(at("C/av/loaders/x64/vulkan-1.dll")).isFile() && QFileInfo(at("C/av/provision.ps1")).isFile(),
          "each file is at update/<drive>/<dirs>/<name>");
    // The bug of a fresh install: one file name with backslashes in it.
    bool no_backslash_names = true;
    for (const QString& name : update.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        no_backslash_names = no_backslash_names && !name.contains(QLatin1Char('\\'));
    }
    for (const QString& path : created) {
        no_backslash_names = no_backslash_names && !QFileInfo(path).fileName().contains(QLatin1Char('\\'));
    }
    check(no_backslash_names, "no file is named with backslashes (the whole guest path as one name)");
    check(!QFileInfo::exists(at("C:")) && !QFileInfo::exists(at("C:/av")), "the drive's colon is not in the tree");
    check(read(at("C/ProgramData/LSW/lsw-agent.exe")) == bundle->files[0].bytes
              && read(at("C/av/package/avgpu.sys")) == bundle->files[1].bytes
              && read(at("C/av/loaders/x64/vulkan-1.dll")) == "vk" && read(at("C/av/provision.ps1")) == "ps",
          "each file's bytes are the bundle's, byte for byte");
    check(read(at("manifest.txt")) == QByteArray::fromStdString(bundle->manifest), "manifest.txt is the bundle's manifest");
    check(read(at("shares.txt")) == QByteArray::fromStdString(bundle->shares), "shares.txt is the bundle's mount list");
    check(read(QDir(staging.path()).filePath(QStringLiteral("autounattend.xml"))) == "<unattend/>",
          "autounattend.xml is at the root");
    bool created_exist = !created.isEmpty();
    for (const QString& path : created) {
        created_exist = created_exist && QFileInfo(path).isFile();
    }
    check(created_exist && created.size() == 1 + 4 + 2, "every file written is reported: the answer file, the bundle's, the two lists");

    // Without a bundle, or with a failed one, no ISO.
    {
        QTemporaryDir other;
        PayloadInputs none;
        none.autounattend_xml = QStringLiteral("<unattend/>");
        QString why;
        check(!PayloadIsoBuilder::stage_files(none, other.path(), nullptr, &why) && !why.isEmpty(),
              "no bundle is a failure that says so");
        auto failed = std::make_shared<GuestBundle::Contents>();
        failed->error = QStringLiteral("cannot find lsw-guest-agent.exe");
        none.bundle = failed;
        why.clear();
        check(!PayloadIsoBuilder::stage_files(none, other.path(), nullptr, &why) && why.contains(QStringLiteral("lsw-guest-agent.exe")),
              "a failed bundle is a failure that carries its reason");
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
