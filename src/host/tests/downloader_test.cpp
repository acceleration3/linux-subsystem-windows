// Hand-rolled (no test framework) downloader smoke tests.
//
// Spins an in-process QTcpServer that speaks just enough HTTP/1.1 to
// exercise the Downloader end to end:
//   * fresh GET of a fixed body
//   * range-resume from an existing partial file
//   * server-side 206 response with the trailing slice
//   * server returns 200 on a Range request (the server ignored Range)
//
// Each scenario runs in a fresh QCoreApplication-driven event loop and
// asserts the on-disk bytes match a known reference.

#include "lsw/host/provision/iso/downloader.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
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

// Tiny in-process HTTP/1.1 server. Each request line is dispatched to
// ``handler``, which returns the full response (status line + headers +
// body) as bytes. The connection is closed after the response.
class MockHttp : public QObject {
public:
    using Handler = std::function<QByteArray(const QByteArray& request_headers)>;

    explicit MockHttp(Handler h) : handler_(std::move(h)) {
        bool ok = server_.listen(QHostAddress::LocalHost, 0);
        if (!ok) {
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
                    return;  // wait for full headers
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

    [[nodiscard]] QString url(const QString& path = "/file") const {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path);
    }

private:
    QTcpServer server_;
    Handler handler_;
    QHash<QTcpSocket*, QByteArray> bufs_;
};

QByteArray http_response(int status, const QByteArray& reason, const QByteArray& body,
                         const QByteArray& extra_headers = {}) {
    QByteArray r;
    r += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Connection: close\r\n";
    if (!extra_headers.isEmpty()) {
        r += extra_headers;
    }
    r += "\r\n";
    r += body;
    return r;
}

// Drive Qt's event loop until ``done`` flips or the timeout elapses.
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

QByteArray make_body(int n) {
    QByteArray b;
    b.reserve(n);
    for (int i = 0; i < n; ++i) {
        b.append(static_cast<char>('A' + (i % 26)));
    }
    return b;
}

void test_fresh_get(const QString& tmp) {
    constexpr int kSize = 4096;
    const QByteArray body = make_body(kSize);
    MockHttp srv([&](const QByteArray&) { return http_response(200, "OK", body); });

    lsw::host::provision::iso::Downloader dl;
    bool finished = false;
    QString final_path;
    QString err;
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::finished, &dl, [&](const QString& p) {
        final_path = p;
        finished = true;
    });
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::failed, &dl, [&](const QString& m) {
        err = m;
        finished = true;
    });

    const QString dest = tmp + "/fresh.bin";
    dl.start(srv.url(), dest);
    wait_for([&] { return finished; });

    check(err.isEmpty(), "fresh: no error");
    check(final_path == dest, "fresh: dest path");
    QFile f{dest};
    check(f.open(QIODevice::ReadOnly), "fresh: open output");
    const auto on_disk = f.readAll();
    check(on_disk == body, "fresh: bytes match");
}

void test_range_resume_206(const QString& tmp) {
    constexpr int kSize = 4096;
    constexpr int kPrefix = 1024;
    const QByteArray body = make_body(kSize);

    bool got_range = false;
    qint64 range_from = -1;
    MockHttp srv([&](const QByteArray& headers) {
        const auto idx = headers.indexOf("Range:");
        if (idx >= 0) {
            got_range = true;
            const auto eol = headers.indexOf("\r\n", idx);
            const auto line = headers.mid(idx, eol - idx);
            // "Range: bytes=1024-"
            const auto eq = line.indexOf('=');
            const auto dash = line.indexOf('-', eq + 1);
            range_from = line.mid(eq + 1, dash - eq - 1).toLongLong();
            const QByteArray slice = body.mid(static_cast<int>(range_from));
            QByteArray cr;
            cr += "Content-Range: bytes ";
            cr += QByteArray::number(range_from);
            cr += "-";
            cr += QByteArray::number(kSize - 1);
            cr += "/";
            cr += QByteArray::number(kSize);
            cr += "\r\n";
            return http_response(206, "Partial Content", slice, cr);
        }
        return http_response(200, "OK", body);
    });

    const QString dest = tmp + "/resume.bin";
    {
        QFile f{dest};
        check(f.open(QIODevice::WriteOnly), "resume: prep partial file");
        f.write(body.left(kPrefix));
    }

    lsw::host::provision::iso::Downloader dl;
    bool finished = false;
    QString err;
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::finished, &dl,
                     [&](const QString&) { finished = true; });
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::failed, &dl, [&](const QString& m) {
        err = m;
        finished = true;
    });
    dl.start(srv.url(), dest);
    wait_for([&] { return finished; });

    check(err.isEmpty(), "resume: no error");
    check(got_range, "resume: server saw Range");
    check(range_from == kPrefix, "resume: Range from correct offset");
    QFile f{dest};
    check(f.open(QIODevice::ReadOnly), "resume: open output");
    const auto on_disk = f.readAll();
    check(on_disk.size() == kSize, "resume: final size matches");
    check(on_disk == body, "resume: bytes match full body");
}

void test_server_ignores_range(const QString& tmp) {
    // Server ignores the Range header and returns 200 with the full
    // body. The downloader is appending to an existing partial file —
    // a naive impl would corrupt the file (prefix + full body). The
    // correct behavior is to truncate and write the full body fresh,
    // OR to abort and surface an error.
    constexpr int kSize = 4096;
    constexpr int kPrefix = 1024;
    const QByteArray body = make_body(kSize);

    MockHttp srv([&](const QByteArray&) { return http_response(200, "OK", body); });

    const QString dest = tmp + "/ignored_range.bin";
    {
        QFile f{dest};
        check(f.open(QIODevice::WriteOnly), "ignored: prep partial file");
        f.write(body.left(kPrefix));
    }

    lsw::host::provision::iso::Downloader dl;
    bool finished = false;
    QString err;
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::finished, &dl,
                     [&](const QString&) { finished = true; });
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::failed, &dl, [&](const QString& m) {
        err = m;
        finished = true;
    });
    dl.start(srv.url(), dest);
    wait_for([&] { return finished; });

    QFile f{dest};
    check(f.open(QIODevice::ReadOnly), "ignored: open output");
    const auto on_disk = f.readAll();
    // The file must be EITHER a clean fresh body OR the call must have
    // failed cleanly. Anything else (e.g. prefix + body) means the
    // downloader corrupted the file.
    const bool clean = on_disk == body;
    const bool failed_cleanly = !err.isEmpty() && on_disk.size() != kPrefix + kSize;
    check(clean || failed_cleanly, "ignored-range: server-ignored-range did not corrupt file");
}

void test_server_error(const QString& tmp) {
    MockHttp srv(
        [&](const QByteArray&) { return http_response(500, "Internal Server Error", "boom"); });

    lsw::host::provision::iso::Downloader dl;
    bool finished = false;
    bool got_failure = false;
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::finished, &dl,
                     [&](const QString&) { finished = true; });
    QObject::connect(&dl, &lsw::host::provision::iso::Downloader::failed, &dl, [&](const QString&) {
        finished = true;
        got_failure = true;
    });
    dl.start(srv.url(), tmp + "/err.bin");
    wait_for([&] { return finished; });
    check(got_failure, "5xx: emits failed");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "FAIL: temp dir\n");
        return 1;
    }

    test_fresh_get(tmp.path());
    test_range_resume_206(tmp.path());
    test_server_ignores_range(tmp.path());
    test_server_error(tmp.path());

    std::fprintf(stderr, "all downloader tests passed\n");
    return 0;
}
