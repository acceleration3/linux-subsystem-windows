#pragma once

namespace lsw::agent::win {
class DisplayScale {
public:
    struct Outcome {
        int displays = 0;
        int percent = 0;
    };

    DisplayScale() = delete;

    [[nodiscard]] static Outcome apply(int percent);
};

}
