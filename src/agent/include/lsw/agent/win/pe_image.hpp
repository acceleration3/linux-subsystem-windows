#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lsw::agent::win {
class PeImage {
public:
    struct ImportedFunction {
        std::string name;
        std::uint16_t ordinal = 0;
        bool by_ordinal = false;
    };

    struct ImportedModule {
        std::string name;
        std::vector<ImportedFunction> functions;
    };

    struct Export {
        std::string forwarder;
    };

    struct ExportTable {
        std::unordered_map<std::string, Export> by_name;
        std::unordered_map<std::uint32_t, Export> by_ordinal;
    };

    [[nodiscard]] static std::unique_ptr<PeImage> open(const std::wstring& path);

    ~PeImage();

    PeImage(const PeImage&) = delete;
    PeImage& operator=(const PeImage&) = delete;

    [[nodiscard]] std::uint16_t machine() const noexcept { return machine_; }

    [[nodiscard]] std::vector<ImportedModule> imports() const;

    [[nodiscard]] const ExportTable& exports() const;

private:
    PeImage() = default;

    struct Section {
        std::uint32_t rva = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };

    [[nodiscard]] bool parse();
    [[nodiscard]] const std::uint8_t* at_rva(std::uint32_t rva, std::size_t length = 1) const;
    [[nodiscard]] std::string string_at(std::uint32_t rva) const;

    UniqueHandle file_;
    UniqueHandle mapping_;
    const std::uint8_t* view_ = nullptr;
    std::size_t size_ = 0;
    std::uint16_t machine_ = 0;
    bool is_64_ = false;
    std::uint32_t import_rva_ = 0;
    std::uint32_t export_rva_ = 0;
    std::uint32_t export_size_ = 0;
    std::vector<Section> sections_;
    mutable std::optional<ExportTable> exports_;
};

}
