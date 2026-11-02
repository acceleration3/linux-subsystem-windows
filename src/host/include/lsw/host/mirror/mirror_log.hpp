#pragma once

#include "lsw/base/rolling_file.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>
#include <string_view>

namespace lsw::host::mirror {
class MirrorLog {
public:
    struct Parsed {
        int level = 2;
        std::string_view tag;
        std::string_view message;
    };

    explicit MirrorLog(const QString& name);
    ~MirrorLog();
    MirrorLog(const MirrorLog&) = delete;
    MirrorLog& operator=(const MirrorLog&) = delete;

    static void set_directory(const QString& directory);

    [[nodiscard]] static QStringList truncate_all();

    [[nodiscard]] static Parsed parse(std::string_view line);

    void roll_for_start();

    void consume(const QByteArray& bytes);

    void finish();

    [[nodiscard]] const QString& name() const noexcept { return name_; }

private:
    void handle_line(std::string_view line_with_newline);

    QString name_;
    QString source_;
    std::unique_ptr<lsw::base::RollingFile> file_;
    QByteArray partial_;
};

}
