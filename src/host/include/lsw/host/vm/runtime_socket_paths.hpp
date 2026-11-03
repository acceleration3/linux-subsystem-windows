#pragma once

#include <QString>

namespace lsw::host::vm {
class RuntimeSocketPaths {
public:
    RuntimeSocketPaths() = delete;

    [[nodiscard]] static QString export_path();
    [[nodiscard]] static QString input_path();

private:
    [[nodiscard]] static QString named(const char* suffix);
};

}
