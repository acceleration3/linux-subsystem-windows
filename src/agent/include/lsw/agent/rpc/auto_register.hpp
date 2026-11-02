#pragma once

#include "lsw/agent/rpc/registry.hpp"

#include <memory>
#include <string>
#include <utility>

namespace lsw::agent::rpc {
template <typename T>
struct AutoRegister {
    explicit AutoRegister(std::string name) {
        RpcRegistry::instance().register_handler(std::move(name),
                                                  std::make_unique<T>());
    }
};

#define LSW_REGISTER_RPC(cls, name)                                         \
    namespace {                                                             \
    [[maybe_unused]] static ::lsw::agent::rpc::AutoRegister<cls> _lsw_reg{  \
        name};                                                              \
    }

}
