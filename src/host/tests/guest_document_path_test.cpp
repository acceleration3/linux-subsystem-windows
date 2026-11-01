// Where a file on this machine is in Windows: the drive of the shared
// folder it sits in, by the longest share that contains it, and nothing
// at all for a file under none or one Windows cannot name.

#include "lsw/host/services/guest_document_path.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::services::GuestDocumentPath;
using lsw::host::vm::DomainDescription;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

DomainDescription::Share share(const char* tag, const char* path) {
    DomainDescription::Share s;
    s.tag = QString::fromLatin1(tag);
    s.host_path = QString::fromLatin1(path);
    return s;
}

std::optional<QString> map(const char* path, const QVector<DomainDescription::Share>& shares) {
    return GuestDocumentPath::map(QString::fromUtf8(path), shares);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    const QVector<DomainDescription::Share> shares = {
        share("z", "/home/u/Documents"),
        share("y", "/home/u/Documents/work"),
        share("x", "/mnt/scratch"),
        share("shares", "/srv"),  // not a drive letter: not LSW's
    };

    check(map("/home/u/Documents/report.xlsx", shares) == QStringLiteral("Z:\\report.xlsx"),
          "a file in a share is on its drive");
    check(map("/home/u/Documents/a/b/c.txt", shares) == QStringLiteral("Z:\\a\\b\\c.txt"),
          "below the share, with backslashes");
    check(map("/home/u/Documents/work/q.docx", shares) == QStringLiteral("Y:\\q.docx"),
          "the longest share wins");
    check(map("/home/u/Documents", shares) == QStringLiteral("Z:\\"), "the share's own folder is its root");
    check(map("/home/u/Documents2/a.txt", shares) == std::nullopt,
          "a sibling whose name starts like the share is not in it");
    check(map("/home/u/Pictures/a.png", shares) == std::nullopt, "outside every share");
    check(map("/srv/a.txt", shares) == std::nullopt, "a tag that is not a drive letter is no share");
    check(map("/mnt/scratch/a.txt", {share("x", "/")}) == QStringLiteral("X:\\mnt\\scratch\\a.txt"),
          "a share of / holds everything");

    check(map("/home/u/Documents/a:b.txt", shares) == std::nullopt, "a colon is illegal");
    check(map("/home/u/Documents/what?.txt", shares) == std::nullopt, "a question mark is illegal");
    check(map("/home/u/Documents/a\"b", shares) == std::nullopt, "a quote is illegal");
    check(map("/home/u/Documents/a|b", shares) == std::nullopt, "a bar is illegal");
    check(map("/home/u/Documents/a<b>c", shares) == std::nullopt, "angle brackets are illegal");
    check(map("/home/u/Documents/a*b", shares) == std::nullopt, "a star is illegal");
    check(map("/home/u/Documents/a\tb", shares) == std::nullopt, "a control character is illegal");
    check(map("/home/u/Documents/name.", shares) == std::nullopt, "a trailing dot is illegal");
    check(map("/home/u/Documents/name ", shares) == std::nullopt, "a trailing space is illegal");
    check(map("/home/u/Documents/CON", shares) == std::nullopt, "CON is reserved");
    check(map("/home/u/Documents/nul.txt", shares) == std::nullopt, "nul.txt is as reserved as nul");
    check(map("/home/u/Documents/Com3.log", shares) == std::nullopt, "COM3 is reserved");
    check(map("/home/u/Documents/lpt9", shares) == std::nullopt, "LPT9 is reserved");
    check(map("/home/u/Documents/COM0", shares).has_value(), "COM0 is not");
    check(map("/home/u/Documents/console.txt", shares).has_value(), "console is not CON");
    check(map("/home/u/Documents/bad:dir/ok.txt", shares) == std::nullopt,
          "an illegal directory part spoils the whole path");
    check(map("/home/u/Documents/100%.txt", shares) == QStringLiteral("Z:\\100%.txt"),
          "a percent sign is a legal name, and left alone");
    check(map("/home/u/Documents/\xc3\xa9t\xc3\xa9.txt", shares).has_value(), "non-ASCII names are legal");
    check(!map("/home/u/Documents/CON:x/y", {share("z", "/home/u/Documents")}).has_value(),
          "an illegal part below the share, whatever follows");
    check(map("/home/u/Documents:odd/a.txt", {share("z", "/home/u/Documents:odd")}).has_value(),
          "the share's own path is not checked, only what is below it");

    // canonical() resolves a link, which is the path virtiofsd would see.
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(QStringLiteral("real"));
    QFile file(dir.path() + QStringLiteral("/real/a.txt"));
    check(file.open(QIODevice::WriteOnly), "a file to link to");
    QFile::link(dir.path() + QStringLiteral("/real"), dir.path() + QStringLiteral("/link"));
    check(GuestDocumentPath::canonical(dir.path() + QStringLiteral("/link/a.txt"))
              == QFileInfo(dir.path() + QStringLiteral("/real/a.txt")).canonicalFilePath(),
          "a symlink is resolved to its target");
    check(GuestDocumentPath::canonical(dir.path() + QStringLiteral("/nothing")).isEmpty(),
          "a missing path has no canonical form");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
