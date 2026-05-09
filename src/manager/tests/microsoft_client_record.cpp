// Standalone recorder. Drives MicrosoftClient against the *real*
// microsoft.com endpoints with ``set_record_directory`` enabled, and
// dumps the five canonical responses (locale, visit, whitelist, skus,
// download_url) to a directory.
//
// Edition IDs rotate on Microsoft's side (2229 used to be Win11 multi-
// edition; today it's a Server preview). To stay current, the recorder
// first calls ``IsoSourceFetcher`` to scrape the live list, picks the
// first Win11 edition, and uses its ``product_edition_ids[0]``. An
// explicit ``edition_id`` argv override skips the scrape.
//
// Usage:
//   lsw_manager_microsoft_client_record <out_dir> [edition_id] [locale]
//     out_dir     — directory to dump NN_<step>.bin files into
//     edition_id  — optional Microsoft ProductEditionId override (skips
//                    the live IsoSourceFetcher scrape)
//     locale      — locale string for stage 1 (defaults to en-US)
//
// The dumped directory is what ``LSW_MICROSOFT_FIXTURE_DIR`` expects,
// so the unit test can replay it deterministically.

#include "lsw/log.hpp"
#include "lsw/manager/iso/microsoft_client.hpp"
#include "lsw/manager/iso/source_fetcher.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <functional>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out_dir> [edition_id] [locale]\n", argv[0]);
        return 2;
    }
    const QString out_dir = QString::fromLocal8Bit(argv[1]);
    const int edition_id_override = (argc >= 3) ? std::atoi(argv[2]) : 0;
    const QString locale = (argc >= 4) ? QString::fromLocal8Bit(argv[3]) : QStringLiteral("en-US");

    ::lsw::log::init("microsoft_client_record");
    QCoreApplication app{argc, argv};
    QDir().mkpath(out_dir);

    lsw::manager::iso::MicrosoftClient client;
    client.set_record_directory(out_dir);

    bool done = false;
    int exit_code = 0;
    int chosen_edition_id = edition_id_override;
    auto fail = [&](const QString& msg) {
        std::fprintf(stderr, "RECORD FAIL: %s\n", msg.toStdString().c_str());
        exit_code = 1;
        done = true;
    };

    QObject::connect(&client, &lsw::manager::iso::MicrosoftClient::initialized, &client, [&] {
        std::fprintf(stderr, "stage 1-3: initialized; using edition_id=%d\n", chosen_edition_id);
        client.fetch_skus(chosen_edition_id);
    });
    QObject::connect(&client, &lsw::manager::iso::MicrosoftClient::initialization_failed, &client,
                     [&](const QString& m) { fail("init: " + m); });

    QObject::connect(&client, &lsw::manager::iso::MicrosoftClient::skus_ready, &client,
                     [&](const QList<lsw::manager::iso::MsSku>& s) {
                         std::fprintf(stderr, "stage 4: skus=%lld\n",
                                      static_cast<long long>(s.size()));
                         if (s.isEmpty()) {
                             fail("no SKUs returned");
                             return;
                         }
                         client.fetch_download_url(s.first());
                     });
    QObject::connect(&client, &lsw::manager::iso::MicrosoftClient::download_url_ready, &client,
                     [&](const QString& u) {
                         std::fprintf(stderr, "stage 5: download url ready (%lld bytes)\n",
                                      static_cast<long long>(u.size()));
                         done = true;
                     });
    QObject::connect(&client, &lsw::manager::iso::MicrosoftClient::failed, &client,
                     [&](const QString& m) { fail(m); });
    QObject::connect(&client, &lsw::manager::iso::MicrosoftClient::banned_detected, &client,
                     [&] { fail("Microsoft banned this IP / region"); });

    // Hard timeout — Microsoft's CDN can hang quite a while on a bad
    // route, but 60s is more than generous for a session that will
    // succeed.
    QTimer::singleShot(60'000, &app, [&] {
        if (!done) {
            fail("timeout (60s) before pipeline completed");
        }
    });

    // If no edition override, scrape one off microsoft.com first.
    // Otherwise jump straight into the recorded pipeline.
    auto kick_off_initialize = [&] { client.initialize(locale); };
    if (chosen_edition_id != 0) {
        std::fprintf(stderr, "edition_id override: %d\n", chosen_edition_id);
        QTimer::singleShot(0, &app, kick_off_initialize);
    } else {
        auto* fetcher = new lsw::manager::iso::IsoSourceFetcher(&app);
        QObject::connect(
            fetcher, &lsw::manager::iso::IsoSourceFetcher::ready, &app,
            [&, fetcher](const QList<lsw::manager::iso::WindowsEdition>& eds) {
                for (const auto& e : eds) {
                    if (e.version.contains("11") && !e.product_edition_ids.isEmpty()) {
                        chosen_edition_id = e.product_edition_ids.first();
                        std::fprintf(stderr, "live scrape: picked %s -> edition_id=%d\n",
                                     e.display.toStdString().c_str(), chosen_edition_id);
                        break;
                    }
                }
                if (chosen_edition_id == 0 && !eds.isEmpty() &&
                    !eds.first().product_edition_ids.isEmpty()) {
                    chosen_edition_id = eds.first().product_edition_ids.first();
                    std::fprintf(stderr,
                                 "live scrape: no Win11 row, falling back to %s -> edition_id=%d\n",
                                 eds.first().display.toStdString().c_str(), chosen_edition_id);
                }
                if (chosen_edition_id == 0) {
                    fail("IsoSourceFetcher returned no usable editions");
                    fetcher->deleteLater();
                    return;
                }
                fetcher->deleteLater();
                kick_off_initialize();
            });
        QObject::connect(fetcher, &lsw::manager::iso::IsoSourceFetcher::failed, &app,
                         [&, fetcher](const QString& m) {
                             fetcher->deleteLater();
                             fail("IsoSourceFetcher: " + m);
                         });
        QTimer::singleShot(0, &app, [fetcher] { fetcher->fetch(); });
    }

    while (!done) {
        app.processEvents(QEventLoop::WaitForMoreEvents, 100);
    }
    return exit_code;
}
