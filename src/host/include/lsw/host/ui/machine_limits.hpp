#pragma once

namespace lsw::host::ui {
struct MachineLimits {
    struct Range {
        int min;
        int max;
        int step;
    };

    static constexpr Range kCores{1, 256, 1};
    static constexpr Range kMemoryMib{2048, 65536, 1024};
    static constexpr Range kDiskGib{30, 512, 5};
};

}
