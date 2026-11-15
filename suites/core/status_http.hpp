#pragma once

#include "core/status_transport.hpp"

#include <string>

namespace lsw::suites {
class StatusHttp final : public StatusTransport {
public:
    explicit StatusHttp(const std::string& url);
    StatusHttp(const StatusHttp&) = delete;
    StatusHttp& operator=(const StatusHttp&) = delete;
    ~StatusHttp() override;

    [[nodiscard]] int post(const Json& batch) override;

private:
    void disconnect();

    std::wstring host_;
    std::wstring path_;
    unsigned short port_ = 0;
    bool secure_ = false;
    void* session_ = nullptr;
    void* connection_ = nullptr;
};
}  // namespace lsw::suites
