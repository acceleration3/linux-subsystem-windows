#pragma once

#include <map>
#include <string>
#include <vector>

namespace lsw::agent::apps {
class FileAssociations {
public:
    using Table = std::map<std::wstring, std::vector<std::wstring>>;

    [[nodiscard]] static Table scan();

    [[nodiscard]] static std::vector<std::wstring> of(const Table& table,
                                                      const std::wstring& program);

    [[nodiscard]] static bool excluded(const std::wstring& extension);
};

}
