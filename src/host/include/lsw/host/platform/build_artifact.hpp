#pragma once

#include <QString>

namespace lsw::host::platform {
[[nodiscard]] QString locate_build_artifact(const QString& env_var, const QString& build_relative,
                                            const QString& installed_relative);

}
