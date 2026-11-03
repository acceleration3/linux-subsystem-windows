#pragma once

#include "lsw/host/platform/host_clipboard.hpp"

#include <QStringList>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <thread>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;

namespace lsw::host::platform {

class DataControlClipboard final : public HostClipboard {
    Q_OBJECT
public:
    explicit DataControlClipboard(QObject* parent = nullptr);
    ~DataControlClipboard() override;

    void start();

    void set(const ClipboardPayload& payload) override;
    void clear_if_owned() override;

    [[nodiscard]] bool working() const override { return !down_.load(); }

    [[nodiscard]] bool limited() const override { return false; }

    struct Offer {
        DataControlClipboard* owner = nullptr;
        ext_data_control_offer_v1* offer = nullptr;
        QStringList types;
    };

    struct Source {
        DataControlClipboard* owner = nullptr;
        ext_data_control_source_v1* source = nullptr;
        ClipboardPayload payload;
    };

    void on_global(wl_registry* registry, unsigned name, const char* interface);
    void on_data_offer(ext_data_control_offer_v1* offer);
    void on_selection(ext_data_control_offer_v1* offer);
    void on_primary_selection(ext_data_control_offer_v1* offer);
    void on_finished();
    void on_send(Source* source, const char* mime, int fd);
    void on_cancelled(Source* source);

Q_SIGNALS:
    void unsupported();

private:
    enum class Kind { Text, Image, Secret };
    enum class Outcome { Stopped, Unsupported, Failed };

    struct Command {
        std::optional<ClipboardPayload> set;
        bool clear = false;
        bool stop = false;
    };

    struct Read {
        int fd = -1;
        Kind kind = Kind::Text;
        bool convert = false;
        qsizetype cap = 0;
        QByteArray bytes;
        std::chrono::steady_clock::time_point deadline;
    };

    void run();
    Outcome session();
    bool open_connection(Outcome& why);
    bool bounded_roundtrip(Outcome& why);
    void close_connection();
    bool serve(Outcome& why);
    bool apply(const Command& command);
    bool flush();
    void wake();
    void drain_wake();
    bool sleep_for(std::chrono::milliseconds how_long);
    void set_connected(bool connected);
    void drop_offer(ext_data_control_offer_v1* offer);
    void start_reads(Offer* offer);
    void add_read(Offer* offer, const QString& mime, Kind kind);
    void service_reads(const std::vector<pollfd>& polled);
    void finish_read(const Read& read, bool ok);
    void reset_reads();
    void publish(const ClipboardPayload& payload);

    std::thread thread_;
    std::mutex mu_;
    Command pending_;
    int wake_fd_ = -1;
    std::atomic<bool> down_{false};

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_seat* seat_ = nullptr;
    ext_data_control_manager_v1* manager_ = nullptr;
    ext_data_control_device_v1* device_ = nullptr;
    std::vector<std::unique_ptr<Offer>> offers_;
    std::vector<std::unique_ptr<Source>> sources_;
    Source* owned_ = nullptr;
    bool device_finished_ = false;
    bool reached_connected_ = false;

    std::vector<Read> reads_;
    ClipboardPayload collected_;
};

}
