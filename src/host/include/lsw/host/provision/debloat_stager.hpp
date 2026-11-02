#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::provision {
class DebloatStager : public QObject {
    Q_OBJECT
public:
    explicit DebloatStager(QObject* parent = nullptr);
    ~DebloatStager() override;

    DebloatStager(const DebloatStager&) = delete;
    DebloatStager& operator=(const DebloatStager&) = delete;
    DebloatStager(DebloatStager&&) = delete;
    DebloatStager& operator=(DebloatStager&&) = delete;

    void trim(const QString& iso_path, const QString& output_dir, int image_index,
              debloat::DebloatLevel level, const QString& file_name = {});

Q_SIGNALS:
    void finished(const QString& iso_path);
    void failed(const QString& message);
    void progress(const QString& detail, int percent);
};

}
