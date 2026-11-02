#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

namespace lsw::host::provision::debloat {
class WimDebloater : public QObject {
    Q_OBJECT
public:
    explicit WimDebloater(QObject* parent = nullptr);

    [[nodiscard]] bool apply(const QString& wim_path, int image_index, const DebloatPlan& plan,
                             QString* error_out);

    [[nodiscard]] static bool isolate_edition(const QString& src_path,
                                              const QStringList& extra_parts, int image_index,
                                              const QString& dest_path, QString* error_out);

    [[nodiscard]] const QStringList& removed() const noexcept { return removed_; }

Q_SIGNALS:
    void progress(const QString& phase, int percent);

private:
    [[nodiscard]] static QStringList children_of(void* wim, int image_index,
                                                 const QString& root);

    [[nodiscard]] static QStringList children_matching(void* wim, int image_index,
                                                       const QString& root,
                                                       const QStringList& prefixes);

    [[nodiscard]] static QStringList children_not_keeping(void* wim, int image_index,
                                                          const QString& root,
                                                          const QStringList& keep);

    QStringList removed_;
};

}
