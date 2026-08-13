// MicrosoftClient end-to-end test.
//
// Spins an in-process MockHttp server and points MicrosoftClient at it
// via ``set_host_overrides``. Each request gets one of five canned
// responses keyed off the URL path. The canned bytes either come from
// inline fixtures (the defaults below) or — when the env var
// ``LSW_MICROSOFT_FIXTURE_DIR`` is set — from files dumped earlier by
// ``MicrosoftClient::set_record_directory(...)``. That lets a
// developer record one real session and replay it deterministically.
//
// Asserts:
//   * initialize() drives locale -> visit -> whitelist and emits
//     ``initialized``.
//   * fetch_skus() parses the SKU list and emits ``skus_ready`` with
//     the expected count.
//   * fetch_download_url() pulls the Uri out and emits
//     ``download_url_ready``.
//   * record_directory writes one file per HTTP round-trip.

#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/provision/iso/microsoft_client.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {

void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
    std::fprintf(stderr, "  ok %s\n", label);
}

// ---------------------------------------------------------------------------
// Fixtures.
// Tiny synthetic JSON for the API endpoints; HTML pages are body-irrelevant
// (the client only uses status + cookies). When env var
// ``LSW_MICROSOFT_FIXTURE_DIR`` points at a directory of NN_<step>.bin files
// dumped by record mode, those override the inline defaults.
// ---------------------------------------------------------------------------

// NB: explicit ``json`` delimiter — the JSON contains ``)"`` (e.g.
// ``"Windows 11 (multi-edition ISO)"`` followed by the trailing quote)
// which would close a default ``R"(...)"`` raw literal early.
constexpr char kSkusJson[] = R"json({
  "Skus": [
    {
      "Id": 2229,
      "Language": "en-US",
      "LocalizedProductDisplayName": "Windows 11 (multi-edition ISO)",
      "FriendlyFileNames": ["Win11_24H2_English_x64.iso"]
    },
    {
      "Id": 2230,
      "Language": "fr-FR",
      "LocalizedProductDisplayName": "Windows 11 (ISO multi-edition)",
      "FriendlyFileNames": ["Win11_24H2_French_x64.iso"]
    }
  ]
})json";

constexpr char kDownloadUrlJson[] = R"json({
  "ProductDownloadOptions": [
    { "Uri": "https://software.download.prss.microsoft.com/dbazure/Win11_24H2_English_x64.iso?token=test" }
  ]
})json";

QByteArray load_fixture(const QString& step, const QByteArray& fallback) {
    const auto dir_env = qEnvironmentVariable("LSW_MICROSOFT_FIXTURE_DIR");
    if (!dir_env.isEmpty()) {
        QDir dir(dir_env);
        const auto entries =
            dir.entryList({QStringLiteral("*_") + step + ".bin"}, QDir::Files, QDir::Name);
        if (!entries.isEmpty()) {
            QFile f(dir.filePath(entries.first()));
            if (f.open(QIODevice::ReadOnly)) {
                return f.readAll();
            }
        }
    }
    return fallback;
}

// ---------------------------------------------------------------------------
// Tiny single-handler HTTP server with optional Content-Length parsing for
// future POST cases. Every reply is "Connection: close" — fine for tests.
// ---------------------------------------------------------------------------
class MockHttp : public QObject {
public:
    using Handler = std::function<QByteArray(const QByteArray& request_headers)>;

    explicit MockHttp(Handler h) : handler_(std::move(h)) {
        if (!server_.listen(QHostAddress::LocalHost, 0)) {
            std::fprintf(stderr, "FAIL: QTcpServer::listen\n");
            std::exit(1);
        }
        QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
            auto* sock = server_.nextPendingConnection();
            QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                QByteArray& buf = bufs_[sock];
                buf.append(sock->readAll());
                const auto end = buf.indexOf("\r\n\r\n");
                if (end < 0) {
                    return;
                }
                const auto reply = handler_(buf.left(end));
                sock->write(reply);
                sock->flush();
                sock->disconnectFromHost();
                bufs_.remove(sock);
            });
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
        });
    }

    [[nodiscard]] QString microsoft_base() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
    }

    [[nodiscard]] QString vlscppe_base() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
    }

private:
    QTcpServer server_;
    Handler handler_;
    QHash<QTcpSocket*, QByteArray> bufs_;
};

QByteArray http_response(int status, const QByteArray& reason, const QByteArray& body,
                         const QByteArray& content_type = "application/json") {
    QByteArray r;
    r += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    r += "Content-Type: " + content_type + "\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Connection: close\r\n";
    r += "\r\n";
    r += body;
    return r;
}

void wait_for(std::function<bool()> done, int timeout_ms = 5000) {
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done()) {
            loop.quit();
        }
    });
    poll.start();
    QTimer::singleShot(timeout_ms, &loop, [&] { loop.quit(); });
    loop.exec();
}

void test_full_pipeline(const QString& tmp_dir) {
    const bool using_real_fixtures = !qEnvironmentVariable("LSW_MICROSOFT_FIXTURE_DIR").isEmpty();
    const QByteArray locale_html = load_fixture("locale", "<html>locale ok</html>");
    const QByteArray visit_html = load_fixture("visit", "<html>visit ok</html>");
    const QByteArray whitelist = load_fixture("whitelist", "");
    const QByteArray skus_json =
        load_fixture("skus", QByteArray::fromRawData(kSkusJson, sizeof(kSkusJson) - 1));
    const QByteArray dl_json = load_fixture(
        "download_url", QByteArray::fromRawData(kDownloadUrlJson, sizeof(kDownloadUrlJson) - 1));

    int hits_locale = 0, hits_visit = 0, hits_whitelist = 0;
    int hits_skus = 0, hits_dl = 0;

    MockHttp srv([&](const QByteArray& headers) {
        // First line is "GET /path HTTP/1.1".
        const auto first_eol = headers.indexOf("\r\n");
        const auto first = headers.left(first_eol);
        const auto sp1 = first.indexOf(' ');
        const auto sp2 = first.indexOf(' ', sp1 + 1);
        const auto path = first.mid(sp1 + 1, sp2 - sp1 - 1);
        if (path.startsWith("/en-US/software-download/") ||
            path.startsWith("/de-DE/software-download/")) {
            ++hits_locale;
            return http_response(200, "OK", locale_html, "text/html");
        }
        if (path.startsWith("/software-download/windows11")) {
            ++hits_visit;
            return http_response(200, "OK", visit_html, "text/html");
        }
        if (path.startsWith("/tags")) {
            ++hits_whitelist;
            return http_response(200, "OK", whitelist, "text/plain");
        }
        if (path.contains("getskuinformationbyproductedition")) {
            ++hits_skus;
            return http_response(200, "OK", skus_json);
        }
        if (path.contains("GetProductDownloadLinksBySku")) {
            ++hits_dl;
            return http_response(200, "OK", dl_json);
        }
        return http_response(404, "Not Found", "");
    });

    lsw::host::provision::iso::MicrosoftClient client;
    client.set_host_overrides(srv.microsoft_base(), srv.vlscppe_base());
    const QString record_dir = tmp_dir + "/record";
    client.set_record_directory(record_dir);

    bool init_ok = false;
    QString init_err;
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::initialized, &client,
                     [&] { init_ok = true; });
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::initialization_failed, &client,
                     [&](const QString& m) { init_err = m; });

    client.initialize("en-US");
    wait_for([&] { return init_ok || !init_err.isEmpty(); });
    check(init_err.isEmpty(), "initialize: no error");
    check(init_ok, "initialize: signal fired");
    check(hits_locale == 1, "initialize: hit locale endpoint");
    check(hits_visit == 1, "initialize: hit visit endpoint");
    check(hits_whitelist == 1, "initialize: hit whitelist endpoint");

    bool got_skus = false;
    QList<lsw::host::provision::iso::MsSku> skus;
    QString skus_err;
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::skus_ready, &client,
                     [&](const QList<lsw::host::provision::iso::MsSku>& s) {
                         skus = s;
                         got_skus = true;
                     });
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::failed, &client,
                     [&](const QString& m) { skus_err = m; });

    client.fetch_skus(2229);
    wait_for([&] { return got_skus || !skus_err.isEmpty(); });
    check(skus_err.isEmpty(), "skus: no error");
    check(got_skus, "skus: signal fired");
    check(hits_skus == 1, "skus: hit SKU endpoint");
    check(skus.size() >= 1, "skus: parsed at least one SKU");
    if (using_real_fixtures) {
        // Real Microsoft uses display names ("English", "French") for
        // ``Language``, not BCP-47 codes — and the edition we end up
        // recording may not have an en-US SKU at all (e.g. server preview
        // editions are English-only with localised display names).
        check(!skus.first().language.isEmpty(), "skus: first sku has a Language");
    } else {
        check(skus.first().language == "en-US", "skus: first sku is en-US");
    }

    bool got_url = false;
    bool got_banned = false;
    QString final_url;
    QString url_err;
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::download_url_ready, &client,
                     [&](const QString& u) {
                         final_url = u;
                         got_url = true;
                     });
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::banned_detected, &client,
                     [&] { got_banned = true; });
    // ``failed`` is reused for download-url errors; clear by reconnecting.
    QObject::disconnect(&client, &lsw::host::provision::iso::MicrosoftClient::failed, nullptr, nullptr);
    QObject::connect(&client, &lsw::host::provision::iso::MicrosoftClient::failed, &client,
                     [&](const QString& m) { url_err = m; });

    client.fetch_download_url(skus.first());
    wait_for([&] { return got_url || got_banned || !url_err.isEmpty(); });
    check(hits_dl == 1, "download_url: hit endpoint");
    if (using_real_fixtures && (got_banned || !url_err.isEmpty())) {
        // Real recordings sometimes capture an Errors envelope:
        //   * Type=9 → 715-123130 IP/region ban (banned_detected)
        //   * Other Type → generic error (failed)
        // Either way the parse path is what we care about; surface
        // and continue.
        std::fprintf(stderr, "  note: real-fixture download_url returned %s\n",
                     got_banned ? "a Type=9 IP/region ban"
                                : ("an envelope error: " + url_err.toStdString()).c_str());
    } else {
        check(url_err.isEmpty(), "download_url: no error");
        check(got_url, "download_url: signal fired");
        check(final_url.startsWith("https://"), "download_url: URL looks like a URL");
    }

    QDir rdir(record_dir);
    const auto recorded = rdir.entryList({"*.bin"}, QDir::Files, QDir::Name);
    check(recorded.size() == 5,
          "record: five files written (locale,visit,whitelist,skus,download_url)");
}

}  // namespace

int main(int argc, char** argv) {
    ::lsw::base::Logger::init("microsoft_client_test");
    QCoreApplication app{argc, argv};
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "FAIL: temp dir\n");
        return 1;
    }
    test_full_pipeline(tmp.path());
    std::fprintf(stderr, "all microsoft_client tests passed\n");
    return 0;
}
