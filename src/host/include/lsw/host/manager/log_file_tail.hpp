#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <sys/types.h>

namespace lsw::host::manager {

class LogFileTail : public QObject {
    Q_OBJECT
public:
    static constexpr int kTailLines = 2000;
    static constexpr int kMaxLines = 20000;
    static constexpr int kPollMs = 500;
    static constexpr qint64 kTailBytes = 512 * 1024;
    static constexpr qint64 kCatchUpBytes = 1024 * 1024;

    explicit LogFileTail(QObject* parent = nullptr);

    void follow(const QString& path);
    void set_active(bool active);

    [[nodiscard]] const QString& path() const noexcept { return path_; }
    [[nodiscard]] const QStringList& lines() const noexcept { return lines_; }
    [[nodiscard]] bool exists() const noexcept { return exists_; }
    [[nodiscard]] bool active() const noexcept { return active_; }

    void poll();

Q_SIGNALS:
    void appended(const QStringList& lines);
    void replaced();

private:
    void reload();
    void forget();

    QString path_;
    QStringList lines_;
    QByteArray partial_;
    QTimer timer_;
    bool active_ = false;
    bool exists_ = false;
    bool loaded_ = false;
    qint64 pos_ = 0;
    dev_t device_ = 0;
    ino_t inode_ = 0;
};

}
