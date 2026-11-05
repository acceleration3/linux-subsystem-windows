#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
class AvStateDump {
public:
    struct Context {
        quint32 id = 0;
        QString name;
        quint64 write_back_bytes = 0;
        quint64 write_combined_bytes = 0;
        int blobs = 0;

        [[nodiscard]] quint64 bytes() const noexcept { return write_back_bytes + write_combined_bytes; }
    };

    struct WindowStats {
        bool attached = false;
        bool stable = false;
        quint64 total_pages = 0;
        quint64 pages_in_use = 0;
        quint64 peak_pages_in_use = 0;
        quint64 refusals = 0;
        quint64 refused_bytes = 0;
        quint64 largest_free_run = 0;
    };

    struct Channel {
        quint32 context = 0;
        quint64 reports = 0;
        quint64 rejected = 0;
        quint64 split_transfers = 0;
        quint64 split_pieces = 0;
        quint64 room_waits = 0;
        quint64 room_wait_us = 0;
        quint64 inline_bulk = 0;
        quint64 copied_mappings = 0;
        quint64 slice_waits = 0;
        quint64 refused_no_space = 0;
        quint64 refused_no_memory = 0;
        quint64 refused_other = 0;

        [[nodiscard]] quint64 refused() const noexcept {
            return refused_no_space + refused_no_memory + refused_other;
        }
    };

    struct Snapshot {
        bool device = false;
        bool executor = false;
        std::optional<quint64> window_bytes;
        std::optional<WindowStats> window_stats;
        QVector<Context> contexts;
        QVector<Channel> channels;

        [[nodiscard]] quint64 used_bytes() const;
        [[nodiscard]] const Context* find_context(quint32 id) const;
    };

    AvStateDump() = delete;

    [[nodiscard]] static Snapshot read(qint64 qemu_pid);

    [[nodiscard]] static QVector<QString> sockets(qint64 qemu_pid);

    [[nodiscard]] static std::optional<QByteArray> fetch(const QString& socket_path);

    static void parse(const QByteArray& dump, Snapshot& into);

    [[nodiscard]] static QHash<QString, QString> fields(const QByteArray& line, QString* path);
};

}
