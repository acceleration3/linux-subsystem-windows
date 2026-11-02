#pragma once

#include "lsw/host/vm/guest_stats_sample.hpp"

namespace lsw::host::vm {
class IGuestStatsProbe {
public:
    IGuestStatsProbe() = default;
    virtual ~IGuestStatsProbe() = default;
    IGuestStatsProbe(const IGuestStatsProbe&) = delete;
    IGuestStatsProbe& operator=(const IGuestStatsProbe&) = delete;

    [[nodiscard]] virtual bool available() const = 0;

    [[nodiscard]] virtual GuestStatsSample sample() = 0;
};

}
