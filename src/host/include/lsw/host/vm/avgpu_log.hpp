#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace lsw::host::vm {
class AvgpuLog {
public:
    struct Facts {
        std::optional<bool> host_window_taken;
        std::optional<bool> user_mode_submission;
        std::optional<bool> native_fences;
        int modes_offered = -1;
        int modes_taken = -1;
        QString preferred_mode;
    };

    static constexpr int kKeptLines = 200;

    explicit AvgpuLog(QString path) : path_(std::move(path)) {}

    bool poll();

    [[nodiscard]] const QString& path() const noexcept { return path_; }
    [[nodiscard]] const QStringList& lines() const noexcept { return lines_; }
    [[nodiscard]] qint64 line_count() const noexcept { return line_count_; }
    [[nodiscard]] const Facts& facts() const noexcept { return facts_; }

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

}
