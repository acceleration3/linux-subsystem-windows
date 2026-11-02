#pragma once

namespace lsw::agent::win {
class GdiPlusRuntime {
public:
    GdiPlusRuntime() = delete;

    static void ensure_started();
};

}
