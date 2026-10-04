#pragma once

namespace lsw::host::ui {

// What the machine's sliders offer, in one place for both pages that
// show them: the wizard sizing the guest before it exists, and Settings
// resizing it afterwards. Two copies of these drift, and then a guest
// the wizard built is one Settings cannot represent.
//
// The wizard narrows the maximums further to what the host has; these
// are the outer bounds either page ever shows.
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

}  // namespace lsw::host::ui
