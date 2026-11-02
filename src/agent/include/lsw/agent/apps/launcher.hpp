#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::apps {
class Launcher {
public:
    using EnvVar = std::pair<std::string, std::string>;
    enum class Mode { NewInstance, Open };

    [[nodiscard]] static bool launch(const std::string& app_id, const std::string& args,
                                     const std::vector<EnvVar>& env, const win::RunAs& as,
                                     unsigned long* pid_out = nullptr,
                                     std::string* error_out = nullptr,
                                      Mode mode = Mode::NewInstance,
                                      const std::vector<unsigned long>& visible_pids = {},
                                      const std::vector<std::string>& documents = {},
                                      std::wstring* image_out = nullptr);
};

}
