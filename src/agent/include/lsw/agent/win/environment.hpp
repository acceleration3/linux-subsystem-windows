#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::win {

// The process environment, as the agent reads it and hands it on.
class Environment {
public:
    Environment() = delete;

    // One ``NAME=value`` override, UTF-8 as it arrives over the wire.
    using Override = std::pair<std::string, std::string>;

    // ``%VARIABLE%`` references in ``s`` expanded against our
    // environment. ``s`` unchanged when expansion fails, so a caller
    // never ends up with less than it started with.
    [[nodiscard]] static std::wstring expand(const std::wstring& s);

    // A child's environment block: ours, with ``overrides`` applied on
    // top, laid out the way CreateProcessW wants it — NAME=value strings
    // each NUL-terminated, the whole block terminated by a second NUL.
    //
    // Keyed case-insensitively, so an override replaces a
    // differently-cased variable rather than adding a twin, and sorted,
    // because that is what the documentation asks of an environment
    // block — and a block that merely happens to work is the kind of
    // thing that stops working on somebody else's machine.
    //
    // Empty when there is nothing to override: the caller then passes
    // nullptr and the child inherits ours directly — no copy, no chance
    // of this code mangling an environment nobody asked it to touch.
    [[nodiscard]] static std::vector<wchar_t> block_with(const std::vector<Override>& overrides);
};

}  // namespace lsw::agent::win
