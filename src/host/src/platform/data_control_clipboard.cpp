#include "lsw/host/platform/data_control_clipboard.hpp"

#include "ext-data-control-v1-client-protocol.h"
#include "lsw/base/log.hpp"

#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>

namespace lsw::host::platform {

namespace {
constexpr const char* kTextMimes[] = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING",
                                      "STRING", "TEXT"};
constexpr const char* kReadTextMimes[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"};
constexpr const char* kPngMime = "image/png";

bool is_text_mime(const char* mime) {
    return std::any_of(std::begin(kTextMimes), std::end(kTextMimes),
                       [mime](const char* t) { return std::strcmp(t, mime) == 0; });
}

QString pick_image_mime(const QStringList& types) {
    if (types.contains(QLatin1String(kPngMime))) {
        return QString::fromLatin1(kPngMime);
    }
    const auto supported = QImageReader::supportedMimeTypes();
    for (const QString& type : types) {
        if (type.startsWith(QLatin1String("image/")) && supported.contains(type.toLatin1())) {
            return type;
        }
    }
    return {};
}

QString pick_text_mime(const QStringList& types) {
    for (const char* mime : kReadTextMimes) {
        if (types.contains(QLatin1String(mime))) {
            return QString::fromLatin1(mime);
        }
    }
    return {};
}

void write_all_and_close(int fd, QByteArray bytes) {
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &block, nullptr);
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const char* data = bytes.constData();
    qsizetype left = bytes.size();
    while (left > 0) {
        const ssize_t put = ::write(fd, data, static_cast<size_t>(left));
        if (put > 0) {
            data += put;
            left -= put;
            continue;
        }
        if (put < 0 && errno == EINTR) {
            continue;
        }
        if (put == 0 || errno != EAGAIN) {
            break;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }
        pollfd out{fd, POLLOUT, 0};
        ::poll(&out, 1, static_cast<int>(remaining.count()));
    }
    ::close(fd);
}

void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface,
                     uint32_t) {
    static_cast<DataControlClipboard*>(data)->on_global(registry, name, interface);
}

void registry_remove(void*, wl_registry*, uint32_t) {}

void sync_done(void* data, wl_callback* callback, uint32_t) {
    *static_cast<bool*>(data) = true;
    wl_callback_destroy(callback);
}

const wl_callback_listener kSync = {sync_done};

constexpr std::chrono::seconds kHandshakeTimeout{5};

const wl_registry_listener kRegistry = {registry_global, registry_remove};

void device_data_offer(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
    static_cast<DataControlClipboard*>(data)->on_data_offer(offer);
}

void device_selection(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
    static_cast<DataControlClipboard*>(data)->on_selection(offer);
}

void device_finished(void* data, ext_data_control_device_v1*) {
    static_cast<DataControlClipboard*>(data)->on_finished();
}

void device_primary(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
    static_cast<DataControlClipboard*>(data)->on_primary_selection(offer);
}

const ext_data_control_device_v1_listener kDevice = {device_data_offer, device_selection,
                                                     device_finished, device_primary};

void offer_type(void* data, ext_data_control_offer_v1*, const char* mime) {
    static_cast<DataControlClipboard::Offer*>(data)->types << QString::fromUtf8(mime);
}

const ext_data_control_offer_v1_listener kOffer = {offer_type};

void source_send(void* data, ext_data_control_source_v1*, const char* mime, int32_t fd) {
    auto* source = static_cast<DataControlClipboard::Source*>(data);
    source->owner->on_send(source, mime, fd);
}

void source_cancelled(void* data, ext_data_control_source_v1*) {
    auto* source = static_cast<DataControlClipboard::Source*>(data);
    source->owner->on_cancelled(source);
}

const ext_data_control_source_v1_listener kSource = {source_send, source_cancelled};
}

DataControlClipboard::DataControlClipboard(QObject* parent) : HostClipboard(parent) {
    wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

DataControlClipboard::~DataControlClipboard() {
    {
        const std::lock_guard lock(mu_);
        pending_.stop = true;
    }
    wake();
    if (thread_.joinable()) {
        thread_.join();
    }
    if (wake_fd_ >= 0) {
        ::close(wake_fd_);
    }
}

void DataControlClipboard::start() {
    thread_ = std::thread{[this] { run(); }};
}

void DataControlClipboard::wake() {
    const std::uint64_t one = 1;
    (void)!::write(wake_fd_, &one, sizeof(one));
}

void DataControlClipboard::drain_wake() {
    std::uint64_t value = 0;
    (void)!::read(wake_fd_, &value, sizeof(value));
}

void DataControlClipboard::set(const ClipboardPayload& payload) {
    {
        const std::lock_guard lock(mu_);
        pending_.set = payload;
        pending_.clear = false;
    }
    wake();
}

void DataControlClipboard::clear_if_owned() {
    {
        const std::lock_guard lock(mu_);
        pending_.set.reset();
        pending_.clear = true;
    }
    wake();
}

void DataControlClipboard::set_connected(bool connected) {
    down_.store(!connected);
    QMetaObject::invokeMethod(this, [this] { Q_EMIT state_changed(); }, Qt::QueuedConnection);
}

void DataControlClipboard::publish(const ClipboardPayload& payload) {
    QMetaObject::invokeMethod(
        this, [this, payload] { Q_EMIT changed(payload); }, Qt::QueuedConnection);
}

bool DataControlClipboard::sleep_for(std::chrono::milliseconds how_long) {
    pollfd fd{wake_fd_, POLLIN, 0};
    ::poll(&fd, 1, static_cast<int>(how_long.count()));
    drain_wake();
    const std::lock_guard lock(mu_);
    return pending_.stop;
}

void DataControlClipboard::run() {
    std::chrono::milliseconds backoff{1000};
    bool first = true;
    for (;;) {
        reached_connected_ = false;
        const Outcome outcome = session();
        set_connected(false);
        if (reached_connected_) {
            backoff = std::chrono::milliseconds{1000};
        }
        if (outcome == Outcome::Stopped) {
            return;
        }
        if (outcome == Outcome::Unsupported && first) {
            QMetaObject::invokeMethod(this, [this] { Q_EMIT unsupported(); }, Qt::QueuedConnection);
            return;
        }
        first = false;
        LSW_DEBUG("clipboard: data control connection down, retrying in {} ms", backoff.count());
        if (sleep_for(backoff)) {
            return;
        }
        backoff = std::min(backoff * 2, std::chrono::milliseconds{30000});
    }
}

DataControlClipboard::Outcome DataControlClipboard::session() {
    Outcome why = Outcome::Failed;
    if (!open_connection(why)) {
        close_connection();
        return why;
    }
    set_connected(true);
    reached_connected_ = true;
    const bool stopped = serve(why);
    close_connection();
    return stopped ? Outcome::Stopped : why;
}

bool DataControlClipboard::bounded_roundtrip(Outcome& why) {
    bool done = false;
    wl_callback* callback = wl_display_sync(display_);
    wl_callback_add_listener(callback, &kSync, &done);
    const auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    const int display_fd = wl_display_get_fd(display_);
    why = Outcome::Failed;
    while (!done) {
        while (wl_display_prepare_read(display_) != 0) {
            if (wl_display_dispatch_pending(display_) < 0) {
                return false;
            }
        }
        if (done) {
            wl_display_cancel_read(display_);
            break;
        }
        if (!flush()) {
            wl_display_cancel_read(display_);
            return false;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            wl_display_cancel_read(display_);
            return false;
        }
        pollfd fds[2] = {{display_fd, POLLIN, 0}, {wake_fd_, POLLIN, 0}};
        const int ready = ::poll(fds, 2, static_cast<int>(left.count()));
        if (ready > 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (wl_display_read_events(display_) < 0) {
                return false;
            }
        } else {
            wl_display_cancel_read(display_);
        }
        if (ready > 0 && (fds[1].revents & POLLIN) != 0) {
            drain_wake();
            const std::lock_guard lock(mu_);
            if (pending_.stop) {
                why = Outcome::Stopped;
                return false;
            }
        }
        if (wl_display_dispatch_pending(display_) < 0) {
            return false;
        }
    }
    return true;
}

bool DataControlClipboard::open_connection(Outcome& why) {
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        why = Outcome::Unsupported;
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistry, this);
    if (!bounded_roundtrip(why)) {
        return false;
    }
    if (seat_ == nullptr || manager_ == nullptr) {
        LSW_DEBUG("clipboard: the compositor has no ext_data_control_manager_v1");
        why = Outcome::Unsupported;
        return false;
    }
    device_finished_ = false;
    device_ = ext_data_control_manager_v1_get_data_device(manager_, seat_);
    ext_data_control_device_v1_add_listener(device_, &kDevice, this);
    return flush();
}

void DataControlClipboard::close_connection() {
    reset_reads();
    for (auto& source : sources_) {
        ext_data_control_source_v1_destroy(source->source);
    }
    sources_.clear();
    owned_ = nullptr;
    for (auto& offer : offers_) {
        ext_data_control_offer_v1_destroy(offer->offer);
    }
    offers_.clear();
    if (device_ != nullptr) {
        ext_data_control_device_v1_destroy(device_);
        device_ = nullptr;
    }
    if (manager_ != nullptr) {
        ext_data_control_manager_v1_destroy(manager_);
        manager_ = nullptr;
    }
    if (seat_ != nullptr) {
        wl_seat_destroy(seat_);
        seat_ = nullptr;
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

bool DataControlClipboard::flush() {
    for (;;) {
        if (wl_display_flush(display_) >= 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        return errno == EAGAIN;
    }
}

bool DataControlClipboard::apply(const Command& command) {
    if (command.clear) {
        if (owned_ != nullptr) {
            ext_data_control_device_v1_set_selection(device_, nullptr);
        }
        return true;
    }
    if (!command.set) {
        return true;
    }
    const ClipboardPayload& payload = *command.set;
    auto entry = std::make_unique<Source>();
    entry->owner = this;
    entry->payload = payload;
    entry->source = ext_data_control_manager_v1_create_data_source(manager_);
    ext_data_control_source_v1_add_listener(entry->source, &kSource, entry.get());
    if (payload.text) {
        for (const char* mime : kTextMimes) {
            ext_data_control_source_v1_offer(entry->source, mime);
        }
    }
    if (!payload.png.isEmpty()) {
        ext_data_control_source_v1_offer(entry->source, kPngMime);
    }
    if (payload.secret) {
        ext_data_control_source_v1_offer(entry->source, kSecretMime);
    }
    ext_data_control_source_v1_offer(entry->source, kMarkerMime);
    ext_data_control_device_v1_set_selection(device_, entry->source);
    owned_ = entry.get();
    sources_.push_back(std::move(entry));
    return true;
}

bool DataControlClipboard::serve(Outcome& why) {
    const int display_fd = wl_display_get_fd(display_);
    for (;;) {
        Command command;
        {
            const std::lock_guard lock(mu_);
            command = std::move(pending_);
            pending_ = Command{};
            if (command.stop) {
                return true;
            }
        }
        apply(command);
        while (wl_display_prepare_read(display_) != 0) {
            if (wl_display_dispatch_pending(display_) < 0) {
                why = Outcome::Failed;
                return false;
            }
        }
        if (!flush()) {
            wl_display_cancel_read(display_);
            why = Outcome::Failed;
            return false;
        }
        std::vector<pollfd> fds;
        fds.push_back({display_fd, POLLIN, 0});
        fds.push_back({wake_fd_, POLLIN, 0});
        for (const Read& read : reads_) {
            fds.push_back({read.fd, POLLIN, 0});
        }
        int timeout = -1;
        const auto now = std::chrono::steady_clock::now();
        for (const Read& read : reads_) {
            const auto left =
                std::chrono::duration_cast<std::chrono::microseconds>(read.deadline - now).count();
            const int ms = static_cast<int>(std::max<long long>((left + 999) / 1000, 0));
            timeout = timeout < 0 ? ms : std::min(timeout, ms);
        }
        const int ready = ::poll(fds.data(), fds.size(), timeout);
        if (ready > 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (wl_display_read_events(display_) < 0) {
                why = Outcome::Failed;
                return false;
            }
        } else {
            wl_display_cancel_read(display_);
        }
        if (ready > 0 && (fds[1].revents & POLLIN) != 0) {
            drain_wake();
        }
        if (wl_display_dispatch_pending(display_) < 0 || wl_display_get_error(display_) != 0 ||
            device_finished_) {
            why = Outcome::Failed;
            return false;
        }
        fds.erase(fds.begin(), fds.begin() + 2);
        service_reads(ready > 0 ? fds : std::vector<pollfd>{});
    }
}

void DataControlClipboard::on_global(wl_registry* registry, unsigned name, const char* interface) {
    if (seat_ == nullptr && std::strcmp(interface, wl_seat_interface.name) == 0) {
        seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (manager_ == nullptr &&
               std::strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
        manager_ = static_cast<ext_data_control_manager_v1*>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
    }
}

void DataControlClipboard::on_data_offer(ext_data_control_offer_v1* offer) {
    auto entry = std::make_unique<Offer>();
    entry->owner = this;
    entry->offer = offer;
    ext_data_control_offer_v1_add_listener(offer, &kOffer, entry.get());
    offers_.push_back(std::move(entry));
}

void DataControlClipboard::drop_offer(ext_data_control_offer_v1* offer) {
    const auto it = std::find_if(offers_.begin(), offers_.end(),
                                 [offer](const auto& o) { return o->offer == offer; });
    if (it == offers_.end()) {
        return;
    }
    ext_data_control_offer_v1_destroy((*it)->offer);
    offers_.erase(it);
}

void DataControlClipboard::on_primary_selection(ext_data_control_offer_v1* offer) {
    if (offer != nullptr) {
        drop_offer(offer);
    }
}

void DataControlClipboard::on_selection(ext_data_control_offer_v1* offer) {
    reset_reads();
    for (auto it = offers_.begin(); it != offers_.end();) {
        if ((*it)->offer != offer) {
            ext_data_control_offer_v1_destroy((*it)->offer);
            it = offers_.erase(it);
        } else {
            ++it;
        }
    }
    if (offer == nullptr || offers_.empty()) {
        owned_ = nullptr;
        QMetaObject::invokeMethod(this, [this] { Q_EMIT cleared(); }, Qt::QueuedConnection);
        return;
    }
    Offer* current = offers_.front().get();
    if (current->types.contains(QString::fromLatin1(kMarkerMime))) {
        return;
    }
    owned_ = nullptr;
    start_reads(current);
}

void DataControlClipboard::on_finished() {
    LSW_WARN("clipboard: the compositor ended the data control device");
    device_finished_ = true;
}

void DataControlClipboard::start_reads(Offer* offer) {
    collected_ = {};
    const QString text = pick_text_mime(offer->types);
    const QString image = pick_image_mime(offer->types);
    LSW_DEBUG("clipboard: host selection text={} image={}", !text.isEmpty(), !image.isEmpty());
    if (!text.isEmpty()) {
        add_read(offer, text, Kind::Text);
    }
    if (!image.isEmpty()) {
        add_read(offer, image, Kind::Image);
    }
    if (offer->types.contains(QString::fromLatin1(kSecretMime))) {
        add_read(offer, QString::fromLatin1(kSecretMime), Kind::Secret);
    }
}

void DataControlClipboard::add_read(Offer* offer, const QString& mime, Kind kind) {
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        return;
    }
    ext_data_control_offer_v1_receive(offer->offer, mime.toUtf8().constData(), fds[1]);
    wl_display_flush(display_);
    ::close(fds[1]);
    Read read;
    read.fd = fds[0];
    read.kind = kind;
    read.convert = kind == Kind::Image && mime != QLatin1String(kPngMime);
    read.cap =
        kind == Kind::Text ? kMaxTextBytes : (read.convert ? 4 * kMaxImageBytes : kMaxImageBytes);
    if (kind == Kind::Secret) {
        read.cap = 64;
    }
    read.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    reads_.push_back(std::move(read));
}

void DataControlClipboard::service_reads(const std::vector<pollfd>& polled) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<Read> finished_ok;
    std::vector<Read> finished_bad;
    std::vector<Read> keep;
    for (std::size_t i = 0; i < reads_.size(); ++i) {
        Read read = std::move(reads_[i]);
        bool done = false;
        bool ok = false;
        short revents = 0;
        for (const pollfd& p : polled) {
            if (p.fd == read.fd) {
                revents = p.revents;
            }
        }
        if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            char chunk[16 * 1024];
            for (;;) {
                const ssize_t got = ::read(read.fd, chunk, sizeof(chunk));
                if (got > 0) {
                    read.bytes.append(chunk, got);
                    if (read.bytes.size() > read.cap) {
                        done = true;
                        break;
                    }
                    continue;
                }
                if (got == 0) {
                    done = true;
                    ok = true;
                } else if (errno == EINTR) {
                    continue;
                } else if (errno != EAGAIN) {
                    done = true;
                }
                break;
            }
        }
        if (!done && now >= read.deadline) {
            done = true;
        }
        if (!done) {
            keep.push_back(std::move(read));
            continue;
        }
        ::close(read.fd);
        read.fd = -1;
        (ok ? finished_ok : finished_bad).push_back(std::move(read));
    }
    reads_ = std::move(keep);
    for (const Read& read : finished_ok) {
        finish_read(read, true);
    }
    for (const Read& read : finished_bad) {
        finish_read(read, false);
    }
    if (reads_.empty() && (!finished_ok.empty() || !finished_bad.empty())) {
        if (!collected_.empty()) {
            LSW_DEBUG("clipboard: host changed text={} png={}B", collected_.text.has_value(),
                      collected_.png.size());
            publish(collected_);
        }
        collected_ = {};
    }
}

void DataControlClipboard::finish_read(const Read& read, bool ok) {
    if (!ok || read.bytes.isEmpty()) {
        return;
    }
    switch (read.kind) {
    case Kind::Text:
        collected_.text = QString::fromUtf8(read.bytes);
        break;
    case Kind::Image: {
        const QByteArray png = read.convert ? png_of(QImage::fromData(read.bytes)) : read.bytes;
        if (png.size() <= kMaxImageBytes) {
            collected_.png = png;
        }
        break;
    }
    case Kind::Secret:
        collected_.secret = read.bytes.trimmed() == "secret";
        break;
    }
}

void DataControlClipboard::reset_reads() {
    for (const Read& read : reads_) {
        ::close(read.fd);
    }
    reads_.clear();
    collected_ = {};
}

void DataControlClipboard::on_send(Source* source, const char* mime, int fd) {
    QByteArray bytes;
    if (is_text_mime(mime) && source->payload.text) {
        bytes = source->payload.text->toUtf8();
    } else if (std::strcmp(mime, kPngMime) == 0) {
        bytes = source->payload.png;
    } else if (std::strcmp(mime, kSecretMime) == 0 && source->payload.secret) {
        bytes = "secret";
    }
    LSW_DEBUG("clipboard: sending {}B to a host application", bytes.size());
    std::thread{write_all_and_close, fd, std::move(bytes)}.detach();
}

void DataControlClipboard::on_cancelled(Source* source) {
    const auto it = std::find_if(sources_.begin(), sources_.end(),
                                 [source](const auto& s) { return s.get() == source; });
    if (it == sources_.end()) {
        return;
    }
    if (owned_ == source) {
        owned_ = nullptr;
    }
    ext_data_control_source_v1_destroy((*it)->source);
    sources_.erase(it);
}

}
