#include "core/status_reporter.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace lsw::suites {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kLinger = std::chrono::milliseconds(250);
constexpr auto kHeartbeat = std::chrono::seconds(5);
constexpr auto kRetryFirst = std::chrono::seconds(1);
constexpr auto kRetryLongest = std::chrono::seconds(15);
constexpr std::size_t kReasonLimit = 400;
constexpr int kStatusCount = 5;

}  // namespace

class StatusReporter::Channel {
public:
    Channel(std::unique_ptr<StatusTransport> transport, Identity identity,
            std::vector<std::string> planned)
        : transport_(std::move(transport)), identity_(std::move(identity)),
          planned_(std::move(planned)) {}

    void started(const std::string& id, std::uint32_t pid) {
        Event event;
        event.kind = Kind::Start;
        event.id = id;
        event.pid = pid;
        event.at = Clock::now();
        const std::lock_guard<std::mutex> lock(mutex_);
        running_.push_back(event);
        queue(std::move(event));
    }

    void finished(const CheckResult& result) {
        Event event;
        event.kind = Kind::Finish;
        event.id = result.id;
        event.pid = result.pid;
        event.status = result.status;
        event.reason = result.reason.substr(0, kReasonLimit);
        event.ms = result.ms;
        event.at = Clock::now();
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto match = std::find_if(running_.begin(), running_.end(), [&](const Event& other) {
            return other.pid == event.pid && other.id == event.id;
        });
        if (match != running_.end()) {
            running_.erase(match);
        }
        ++counts_[static_cast<int>(event.status)];
        finished_.push_back(event);
        queue(std::move(event));
    }

    void stop(bool complete) {
        std::unique_lock<std::mutex> lock(mutex_);
        Event event;
        event.kind = Kind::End;
        event.complete = complete;
        event.at = Clock::now();
        end_ = event;
        queue(std::move(event));
        stop_ = true;
        wake_.notify_all();
    }

    [[nodiscard]] bool wait_exit(std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_.wait_for(lock, limit, [&] { return exited_; });
    }

    void send_loop() {
        std::unique_lock<std::mutex> lock(mutex_);
        auto retry = std::chrono::duration_cast<Clock::duration>(kRetryFirst);
        Clock::time_point not_before = Clock::time_point::min();
        Clock::time_point last_send = Clock::now();
        bool flushed = false;
        for (;;) {
            while (!stop_) {
                const Clock::time_point now = Clock::now();
                const bool work = resync_ || !pending_.empty();
                if ((work && now >= not_before) || (!work && now - last_send >= kHeartbeat)) {
                    break;
                }
                wake_.wait_until(lock, work ? not_before : last_send + kHeartbeat);
            }
            if (stop_ && !resync_ && pending_.empty()) {
                break;
            }
            if (!stop_ && !resync_ && !pending_.empty()) {
                wake_.wait_for(lock, kLinger, [&] { return stop_ || resync_; });
            }
            const bool last = stop_;
            const bool snapshot = resync_;
            const Json batch = build(snapshot);
            lock.unlock();
            const int code = transport_->post(batch);
            lock.lock();
            last_send = Clock::now();
            if (code >= 200 && code < 300) {
                retry = kRetryFirst;
                not_before = Clock::time_point::min();
            } else if (code == 409 && !snapshot) {
                resync_ = true;
                ++resends_;
            } else {
                resync_ = true;
                ++dropped_;
                not_before = last_send + retry;
                retry = std::min<Clock::duration>(retry * 2, kRetryLongest);
            }
            if (last && (flushed || code != 409 || snapshot)) {
                break;
            }
            flushed = last;
        }
        exited_ = true;
        idle_.notify_all();
    }

private:
    enum class Kind { Start, Finish, End };

    struct Event {
        Kind kind = Kind::Start;
        std::string id;
        std::uint32_t pid = 0;
        Status status = Status::Crash;
        std::string reason;
        long long ms = 0;
        bool complete = false;
        Clock::time_point at;
    };

    void queue(Event event) {
        if (resync_) {
            return;
        }
        if (pending_.size() >= kQueueLimit) {
            pending_.clear();
            resync_ = true;
        } else {
            pending_.push_back(std::move(event));
        }
        wake_.notify_all();
    }

    [[nodiscard]] Json event_json(const Event& event, Clock::time_point now) const {
        Json out = Json::object();
        out.set("t", event.kind == Kind::Start ? "start" : event.kind == Kind::Finish ? "finish" : "end");
        out.set("ago_ms", static_cast<long long>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(now - event.at).count()));
        if (event.kind == Kind::End) {
            out.set("complete", event.complete);
            out.set("crashed", counts_[static_cast<int>(Status::Crash)]);
            out.set("timed_out", counts_[static_cast<int>(Status::Timeout)]);
            out.set("failed", counts_[static_cast<int>(Status::Fail)]);
            return out;
        }
        out.set("id", event.id);
        out.set("pid", event.pid);
        if (event.kind == Kind::Finish) {
            out.set("status", Outcome::name(event.status));
            out.set("reason", event.reason);
            out.set("ms", event.ms);
        }
        return out;
    }

    [[nodiscard]] Json build(bool snapshot) {
        const Clock::time_point now = Clock::now();
        long long done = 0;
        Json totals = Json::object();
        totals.set("planned", planned_.size());
        for (int status = 0; status < kStatusCount; ++status) {
            totals.set(Outcome::name(static_cast<Status>(status)), counts_[status]);
            done += counts_[status];
        }
        totals.set("done", done);
        totals.set("running", running_.size());
        totals.set("dropped", dropped_);
        totals.set("resends", resends_);
        Json events = Json::array();
        Json batch = Json::object();
        batch.set("v", 1);
        batch.set("run", identity_.run);
        batch.set("part", identity_.part);
        batch.set("invocation", identity_.invocation);
        batch.set("seq", ++sequence_);
        batch.set("snapshot", snapshot);
        batch.set("totals", std::move(totals));
        if (snapshot) {
            Json suites = Json::array();
            for (const std::string& suite : identity_.suites) {
                suites.push(suite);
            }
            Json planned = Json::array();
            for (const std::string& id : planned_) {
                planned.push(id);
            }
            Json begin = Json::object();
            begin.set("label", identity_.label);
            begin.set("exe", identity_.exe);
            begin.set("bitness", identity_.bitness);
            begin.set("suites", std::move(suites));
            begin.set("adapter", identity_.adapter);
            begin.set("started", identity_.started);
            begin.set("phase", identity_.phase);
            begin.set("jobs", identity_.jobs);
            begin.set("planned", std::move(planned));
            batch.set("begin", std::move(begin));
            for (const Event& event : finished_) {
                events.push(event_json(event, now));
            }
            for (const Event& event : running_) {
                events.push(event_json(event, now));
            }
            if (end_) {
                events.push(event_json(*end_, now));
            }
        } else {
            for (const Event& event : pending_) {
                events.push(event_json(event, now));
            }
        }
        pending_.clear();
        resync_ = false;
        batch.set("events", std::move(events));
        return batch;
    }

    const std::unique_ptr<StatusTransport> transport_;
    const Identity identity_;
    const std::vector<std::string> planned_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<Event> pending_;
    std::vector<Event> finished_;
    std::vector<Event> running_;
    std::optional<Event> end_;
    long long counts_[kStatusCount] = {};
    long long sequence_ = 0;
    long long dropped_ = 0;
    long long resends_ = 0;
    bool resync_ = true;
    bool stop_ = false;
    bool exited_ = false;
};

StatusReporter::~StatusReporter() {
    close(false);
}

void StatusReporter::open(std::unique_ptr<StatusTransport> transport, Identity identity,
                          std::vector<std::string> planned) {
    close(false);
    channel_ = std::make_shared<Channel>(std::move(transport), std::move(identity), std::move(planned));
    thread_ = std::thread([channel = channel_] { channel->send_loop(); });
}

void StatusReporter::check_started(const std::string& id, std::uint32_t pid) {
    if (channel_) {
        channel_->started(id, pid);
    }
}

void StatusReporter::check_finished(const CheckResult& result) {
    if (channel_) {
        channel_->finished(result);
    }
}

void StatusReporter::close(bool complete) {
    if (!channel_) {
        return;
    }
    channel_->stop(complete);
    if (channel_->wait_exit(std::chrono::milliseconds(kFlushMs))) {
        thread_.join();
    } else {
        thread_.detach();
    }
    channel_.reset();
}

}  // namespace lsw::suites
