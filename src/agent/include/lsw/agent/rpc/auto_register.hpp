#pragma once

#include "lsw/agent/rpc/registry.hpp"

#include <memory>
#include <string>
#include <utility>

namespace lsw::agent::rpc {

// Helper used by the LSW_REGISTER_RPC macro. Constructing one at
// namespace scope inserts the handler into the registry before
// ``main`` runs. The template parameter is the handler class — we
// default-construct it; handlers therefore must have a public
// no-argument constructor (none today have any reason to need
// otherwise).
template <typename T>
struct AutoRegister {
    explicit AutoRegister(std::string name) {
        RpcRegistry::instance().register_handler(std::move(name),
                                                  std::make_unique<T>());
    }
};

// Place ``LSW_REGISTER_RPC(SomeHandler, "some_cmd");`` at the bottom
// of the .cpp file that defines ``SomeHandler``. ``cls`` may be a
// fully-qualified name (e.g. ``lsw::agent::rpc::handlers::Foo``);
// the registrar variable itself lives in an anonymous namespace so
// each TU has its own copy and ``_lsw_reg`` never collides across
// TUs even though all TUs reuse the same identifier.
//
// One registration per TU is the rule (the macro doesn't token-paste
// against ``cls`` for this exact reason — ``cls`` would contain
// ``::`` which is not a valid preprocessor concat). If a single TU
// genuinely needs to register two commands, split it.
//
// The ``[[maybe_unused]]`` is load-bearing on MinGW with -Wall: the
// global has no observable use from within the TU.
#define LSW_REGISTER_RPC(cls, name)                                         \
    namespace {                                                             \
    [[maybe_unused]] static ::lsw::agent::rpc::AutoRegister<cls> _lsw_reg{  \
        name};                                                              \
    }

}  // namespace lsw::agent::rpc
