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
        quint32 device = 0;
        quint32 id = 0;
        QString name;
        quint64 write_back_bytes = 0;
        quint64 write_combined_bytes = 0;
        int blobs = 0;
        std::optional<quint64> batches;

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
        quint64 largest_free_run_at_refusal = 0;
    };

    struct Channel {
        quint32 context = 0;
        quint64 reports = 0;
        quint64 rejected = 0;
        quint64 split_transfers = 0;
        quint64 split_pieces = 0;
        std::optional<quint64> blob_room_waits;
        std::optional<quint64> blob_room_wait_us;
        std::optional<quint64> ring_waits;
        std::optional<quint64> ring_wait_us;
        std::optional<quint64> fence_waits;
        std::optional<quint64> fence_wait_us;
        quint64 inline_bulk = 0;
        quint64 copied_mappings = 0;
        quint64 slice_waits = 0;
        quint64 refused_no_space = 0;
        quint64 refused_no_memory = 0;
        quint64 refused_other = 0;
        std::optional<quint64> arena_grows;
        std::optional<quint64> arena_bytes;
        std::optional<quint64> arena_peak_ring_bytes;
        std::optional<quint64> arena_set_aside_bytes;
        std::optional<quint64> arena_segments;
        std::optional<quint64> parked_blobs;

        [[nodiscard]] quint64 refused() const noexcept {
            return refused_no_space + refused_no_memory + refused_other;
        }
    };

    struct GpuVram {
        quint32 device = 0;
        std::optional<quint64> guest_cap_bytes;
        std::optional<quint64> reserve_bytes;
    };

    struct Snapshot {
        bool device = false;
        bool executor = false;
        std::optional<quint64> window_bytes;
        std::optional<WindowStats> window_stats;
        QVector<Context> contexts;
        QVector<Channel> channels;
        QVector<GpuVram> vram;

        [[nodiscard]] quint64 used_bytes() const;
        [[nodiscard]] const Context* find_context(quint32 id) const;
        [[nodiscard]] const Context* find_context(quint32 device, quint32 id) const;
        [[nodiscard]] int contexts_with_id(quint32 id) const;
    };

    AvStateDump() = delete;

    [[nodiscard]] static Snapshot read(qint64 qemu_pid);

    [[nodiscard]] static QVector<QString> sockets(qint64 qemu_pid);

    [[nodiscard]] static std::optional<QByteArray> fetch(const QString& socket_path);

    static void parse(const QByteArray& dump, Snapshot& into);

    [[nodiscard]] static QHash<QString, QString> fields(const QByteArray& line, QString* path);
};

}
