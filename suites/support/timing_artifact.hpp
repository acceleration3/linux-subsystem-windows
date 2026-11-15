#pragma once

#include "core/context.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
class TimingArtifact {
public:
    TimingArtifact(Context& context, std::string name, const std::string& header);

    void row(const std::vector<std::string>& cells);

    bool write();

    static bool text(Context& context, const std::string& file_name, const std::string& content);

private:
    Context& context_;
    std::string name_;
    std::string content_;
};
}  // namespace lsw::suites
