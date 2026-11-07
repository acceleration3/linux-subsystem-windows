#pragma once

#include "support/gl_enums.hpp"
#include "support/gl_function_list.hpp"
#include "support/gl_library.hpp"

#include <cstddef>
#include <set>
#include <string>

namespace lsw::suites {
class GlApi {
public:
#define LSW_GL_MEMBER(ret, name, ...) ret(__stdcall* name)(__VA_ARGS__) = nullptr;
    LSW_GL_FUNCTIONS(LSW_GL_MEMBER)
#undef LSW_GL_MEMBER

    [[nodiscard]] bool load(const GlLibrary& library, std::string& error);

    [[nodiscard]] int major() const { return major_; }

    [[nodiscard]] int minor() const { return minor_; }

    [[nodiscard]] int version() const { return major_ * 10 + minor_; }

    [[nodiscard]] bool at_least(int major, int minor) const { return version() >= major * 10 + minor; }

    [[nodiscard]] bool has_extension(const std::string& name) const { return extensions_.count(name) != 0; }

    [[nodiscard]] bool supports(int major, int minor, const char* extension) const {
        return at_least(major, minor) || (extension != nullptr && has_extension(extension));
    }

    [[nodiscard]] const std::set<std::string>& extensions() const { return extensions_; }

    [[nodiscard]] std::string text(unsigned name) const;

    [[nodiscard]] int integer(unsigned name) const;

    unsigned drain_errors() const;

    [[nodiscard]] static std::string error_name(unsigned code);

private:
    int major_ = 0;
    int minor_ = 0;
    std::set<std::string> extensions_;
};
}  // namespace lsw::suites
