#pragma once

namespace lsw::host::platform {
class QtLogBridge {
public:
    QtLogBridge() = delete;

    static void install();
};

}
