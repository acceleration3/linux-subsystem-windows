#pragma once

#include <QtGlobal>

namespace lsw::host::vm {
struct GuestStatsSample {
    bool valid = false;
    int cpu_percent = -1;
    quint64 mem_total = 0;
    quint64 mem_avail = 0;
    quint64 disk_total = 0;
    quint64 disk_avail = 0;
};

}
