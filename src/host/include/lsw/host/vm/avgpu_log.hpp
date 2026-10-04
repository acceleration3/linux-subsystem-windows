#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace lsw::host::vm {

// A tail of the guest display driver's own log.
//
// avgpu.sys writes "avgpu: ..." lines to I/O port 0x402, and the
// domain's isa-debugcon puts them in a host file
// (Settings::vm_avgpu_log_path) that QEMU truncates each time it starts.
// It keeps the lines across guest reboots and up to a bugcheck, so it
// has the last word when Windows does not.
//
// ``poll`` follows the file by size — it is truncated and recreated
// under QEMU's control, which a file watcher loses track of — keeps the
// last lines, and picks out the few status lines the Graphics page
// reports. Whoever owns it decides how often.
class AvgpuLog {
public:
    // What the driver said about itself, from its status lines. Each is
    // unset until the line has been seen in this run of the VM.
    struct Facts {
        // "avgpu: host window status 0x..": 0 is the window taken.
        std::optional<bool> host_window_taken;
        std::optional<bool> user_mode_submission;
        std::optional<bool> native_fences;
        // "avgpu: the host offers N modes, M taken, preferred WxH".
        int modes_offered = -1;
        int modes_taken = -1;
        QString preferred_mode;  // "2560x1440"
    };

    static constexpr int kKeptLines = 200;

    explicit AvgpuLog(QString path) : path_(std::move(path)) {}

    // Read what was added since the last call. True when anything
    // changed: new lines, or the file truncated or gone.
    bool poll();

    [[nodiscard]] const QString& path() const noexcept { return path_; }
    // The last lines, oldest first.
    [[nodiscard]] const QStringList& lines() const noexcept { return lines_; }
    // Every line in this run of the file, counted.
    [[nodiscard]] qint64 line_count() const noexcept { return line_count_; }
    [[nodiscard]] const Facts& facts() const noexcept { return facts_; }

    // ``line``'s contribution to ``facts``. Public for the tests.
    static void read_fact(const QString& line, Facts& facts);

private:
    void reset();

    QString path_;
    qint64 offset_ = 0;
    QByteArray partial_;
    QStringList lines_;
    qint64 line_count_ = 0;
    Facts facts_;
};

}  // namespace lsw::host::vm
