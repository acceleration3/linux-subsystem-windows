#pragma once

#include "lsw/agent/win/pe_image.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lsw::agent::apps {
class ImportResolver {
public:
    struct Problem {
        enum class Kind { Missing, WrongBitness, Invalid, MissingExport };

        Kind kind = Kind::Missing;
        std::wstring module;
        std::wstring importer;
        std::wstring function;
        std::wstring path;
        int module_bits = 0;
        int image_bits = 0;
    };

    ImportResolver(std::wstring working_dir, std::wstring path_env);

    [[nodiscard]] std::vector<Problem> check(const std::wstring& image);

private:
    struct Resolution {
        enum class Source { None, ApiSet, Known, Search };

        Source source = Source::None;
        std::wstring path;
    };

    void load_known_dlls();
    [[nodiscard]] Resolution resolve(const std::wstring& module);
    [[nodiscard]] Resolution search(const std::wstring& module) const;
    [[nodiscard]] const win::PeImage* image_at(const std::wstring& path);
    void scan(const std::wstring& importer_path);
    void add(Problem problem);
    [[nodiscard]] bool has_export(const std::wstring& module_path, const std::string& name,
                                  std::uint16_t ordinal, bool by_ordinal, int depth);
    [[nodiscard]] static int bits_of(std::uint16_t machine);

    std::wstring working_dir_;
    std::wstring path_env_;
    std::wstring app_dir_;
    std::wstring system_dir_;
    std::wstring windows_dir_;
    std::uint16_t machine_ = 0;
    std::unordered_set<std::wstring> known_;
    std::unordered_map<std::wstring, Resolution> resolutions_;
    std::unordered_map<std::wstring, std::unique_ptr<win::PeImage>> images_;
    std::unordered_set<std::wstring> scanned_;
    std::unordered_set<std::wstring> reported_;
    std::vector<Problem> problems_;
};

}
