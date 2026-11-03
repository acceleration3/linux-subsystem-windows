#pragma once

#include <QString>

#include <array>

namespace lsw::host::services {
class GuestUpdateSteps {
public:
    struct Entry {
        const char* key;
        const char* title;
        const char* done;
    };

    static constexpr const char* kRestart = "restart";

    [[nodiscard]] static const std::array<Entry, 8>& all();

    [[nodiscard]] static QString title_of(const QString& key);

    [[nodiscard]] static QString done_of(const QString& key);
};

}
