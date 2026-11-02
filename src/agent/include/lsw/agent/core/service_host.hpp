#pragma once

namespace lsw::agent::core {
class ServiceHost {
public:
    ServiceHost() = delete;

    static constexpr const wchar_t* kName = L"lsw-agent";

    [[nodiscard]] static int run();
};

}
