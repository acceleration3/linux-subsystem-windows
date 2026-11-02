#include "lsw/host/services/driver_catalog.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QHash>
#include <QUrl>
#include <cstdio>

using lsw::host::services::DriverCatalog;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

QByteArray row(int number, const char* title) {
    return QByteArray("<tr id=\"0a1b2c3d-0000-4000-8000-00000000000") + QByteArray::number(number) +
           "_R" + QByteArray::number(number) + "\" class=\"resultsbottomBorder\">\n<td> "
           "<a href=\"#\">" + title + "</a> </td><td>Drivers</td><td>10/1/2026</td></tr>\n";
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    {
        const QByteArray page =
            "<html><body>Catalog 9.9.9.9 <script>var v = '99.99.99.99';</script><table>\n" +
            row(0, "Advanced Micro Devices, Inc. Display Driver Update (32.0.22042.14002)") +
            row(1, "Advanced Micro Devices, Inc. Display Driver Update (32.0.31044.16)") +
            row(2, "Advanced Micro Devices, Inc. Display Driver Update (32.0.9.1)") +
            "</table><footer>build 100.0.0.0</footer></body></html>";
        const auto newest = DriverCatalog::newest(page);
        check(newest && *newest == QStringLiteral("32.0.31044.16"),
              "the newest is the numeric maximum across rows, not the longest or the last");
        check(!DriverCatalog::newest("<html>1.2.3.4 <table></table></html>"),
              "four-part numbers outside result rows are not drivers");
        check(!DriverCatalog::newest(""), "no page has no version");
        const QByteArray odd = row(0, "Driver Update (32.0.70000.1)") +
                               row(1, "Driver Update (32.0.31044.16)");
        const auto valid = DriverCatalog::newest(odd);
        check(valid && *valid == QStringLiteral("32.0.31044.16"),
              "a part no file version can hold does not beat a real driver");
    }

    check(DriverCatalog::search_url(QStringLiteral("1002"), QStringLiteral("7550")).toEncoded() ==
              "https://www.catalog.update.microsoft.com/Search.aspx?q=PCI%5CVEN_1002%26DEV_7550",
          "the search names the PCI function");

    {
        const QHash<QString, QByteArray> pages{
            {QStringLiteral("PCI%5CVEN_1002%26DEV_7550"),
             row(0, "AMD Display Driver Update (32.0.31044.16)")},
            {QStringLiteral("PCI%5CVEN_10DE%26DEV_2684"), QByteArray{}},
        };
        int fetches = 0;
        DriverCatalog catalog{[&](const QUrl& url, DriverCatalog::Done done) {
            ++fetches;
            const QString query = QString::fromLatin1(url.toEncoded()).section('=', 1);
            if (pages.value(query).isEmpty()) {
                done({}, QStringLiteral("timed out"));
            } else {
                done(pages.value(query), {});
            }
        }};
        int completions = 0;
        QString result;
        QObject::connect(&catalog, &DriverCatalog::completed, [&](const QString& versioned) {
            ++completions;
            result = versioned;
        });
        catalog.lookup(QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\n"
                                      "10de\t2684\tNVIDIA GeForce RTX 4090\n"
                                      "1002\t7550\tAMD Radeon RX 9070 XT\n"));
        check(completions == 1, "a lookup completes once");
        check(fetches == 2, "one search a distinct vendor and device");
        check(result == QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\t32.0.31044.16\n"
                                       "10de\t2684\tNVIDIA GeForce RTX 4090\n"
                                       "1002\t7550\tAMD Radeon RX 9070 XT\t32.0.31044.16\n"),
              "versions are appended and a failed GPU's line is left alone");
    }

    {
        DriverCatalog catalog{[](const QUrl&, DriverCatalog::Done) {
            std::printf("FAIL a lookup of nothing searched\n");
            ++g_failures;
        }};
        int completions = 0;
        QObject::connect(&catalog, &DriverCatalog::completed, [&](const QString& versioned) {
            completions += versioned.isEmpty() ? 1 : 100;
        });
        catalog.lookup({});
        check(completions == 1, "no lines complete at once, with none");
    }

    {
        QTemporaryDir dir;
        const QString cache = dir.filePath(QStringLiteral("driver-versions.json"));
        const QString names = QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\n");
        const auto lookup = [&](const QString& path, bool succeed, const char* version) {
            DriverCatalog catalog{[&](const QUrl&, DriverCatalog::Done done) {
                                      if (succeed) {
                                          done(row(0, version), {});
                                      } else {
                                          done({}, QStringLiteral("offline"));
                                      }
                                  },
                                  path};
            QString result;
            int completions = 0;
            QObject::connect(&catalog, &DriverCatalog::completed, [&](const QString& v) {
                result = v;
                ++completions;
            });
            catalog.lookup(names);
            check(completions == 1, "a lookup with a cache completes once");
            return result;
        };
        const auto stored = [&] {
            QFile file{cache};
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
        };

        check(lookup(cache, false, "") == names, "neither a lookup nor a cache leaves the lines as they were");
        check(!QFile::exists(cache), "a failed lookup writes no cache");
        check(lookup(cache, true, "AMD Driver (32.0.31044.16)")
                  == QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\t32.0.31044.16\n"),
              "a successful lookup versions the line");
        check(stored().value(QStringLiteral("1002:7550")).toString() == QStringLiteral("32.0.31044.16")
                  && stored().size() == 1,
              "a successful lookup is stored per vendor:device");
        check(lookup(cache, false, "") == QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\t32.0.31044.16\n"),
              "a failed lookup uses the cached version (the release does not change)");
        check(lookup({}, false, "") == names, "with no cache path a failed lookup leaves the fallback");
        check(lookup(cache, true, "AMD Driver (32.0.40000.1)")
                  == QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\t32.0.40000.1\n")
                  && stored().value(QStringLiteral("1002:7550")).toString() == QStringLiteral("32.0.40000.1"),
              "a newer find replaces the cached version in the file");
        check(lookup(cache, false, "") == QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\t32.0.40000.1\n"),
              "and the file round-trips into the next lookup");
        QFile bad{cache};
        bad.open(QIODevice::WriteOnly | QIODevice::Truncate);
        bad.write("{not json");
        bad.close();
        check(lookup(cache, false, "") == names, "a cache file that is not JSON is as good as none");
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
