#pragma once

#include <map>
#include <string>
#include <vector>

namespace lsw::agent::apps {

// Which programs Windows registers for which file types.
//
// Read from HKEY_CLASSES_ROOT: every ".ext" key names a default ProgID
// and, under OpenWithProgids, the others that may open it; and from the
// Capabilities keys RegisteredApplications points at, where a browser or
// Office registers its types instead of under the extension; a ProgID
// either carries the AppUserModelID of a packaged application or the
// command line of its default verb. Reading the registry is what the
// shell's Open With list does, and takes a fraction of a second, so it
// is done afresh for each listing rather than cached.
class FileAssociations {
public:
    // Lowercased program (a full path, or an AppUserModelID) to the
    // extensions ("." included, lowercase) it handles, sorted.
    using Table = std::map<std::wstring, std::vector<std::wstring>>;

    [[nodiscard]] static Table scan();

    // The extensions ``program`` handles in ``table``, for a reply.
    [[nodiscard]] static std::vector<std::wstring> of(const Table& table,
                                                      const std::wstring& program);

    // Whether the extension is one that runs or configures things rather
    // than a document, and so never offered as a file type.
    [[nodiscard]] static bool excluded(const std::wstring& extension);
};

}  // namespace lsw::agent::apps
