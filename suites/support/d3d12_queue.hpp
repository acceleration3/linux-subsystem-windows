#pragma once

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lsw::suites {
class D3d12Queue {
public:
    using Recorder = std::function<void(ID3D12GraphicsCommandList*)>;

    static constexpr DWORD kWaitMs = 20000;

    D3d12Queue() = default;
    D3d12Queue(const D3d12Queue&) = delete;
    D3d12Queue& operator=(const D3d12Queue&) = delete;
    ~D3d12Queue();

    [[nodiscard]] bool open(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, std::string& error,
                            D3D12_COMMAND_QUEUE_PRIORITY priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    [[nodiscard]] bool is_open() const { return queue_ != nullptr; }

    [[nodiscard]] ID3D12CommandQueue* queue() const { return queue_; }

    [[nodiscard]] ID3D12Fence* fence() const { return fence_; }

    [[nodiscard]] ID3D12Device* device() const { return device_; }

    [[nodiscard]] D3D12_COMMAND_LIST_TYPE type() const { return type_; }

    [[nodiscard]] std::uint64_t last_signalled() const { return value_; }

    std::uint64_t signal();

    std::uint64_t execute(ID3D12CommandList* const* lists, UINT count);

    [[nodiscard]] ID3D12CommandList* record(const Recorder& body, std::string& error) { return record_as(type_, body, error); }

    [[nodiscard]] ID3D12CommandList* record_as(D3D12_COMMAND_LIST_TYPE list_type, const Recorder& body, std::string& error);

    [[nodiscard]] bool run(const Recorder& body, std::uint64_t& value, std::string& error);

    [[nodiscard]] bool run_wait(const Recorder& body, std::string& error);

    [[nodiscard]] bool wait(std::uint64_t value, DWORD timeout_ms, std::string& error);

    [[nodiscard]] bool wait(std::uint64_t value, std::string& error) { return wait(value, kWaitMs, error); }

    [[nodiscard]] bool idle(std::string& error);

    [[nodiscard]] bool reached(std::uint64_t value) const;

    [[nodiscard]] bool gpu_wait(D3d12Queue& other, std::uint64_t value);

    void gpu_wait_fence(ID3D12Fence* fence, std::uint64_t value);

    void touch() { dirty_ = true; }

    void retire(IUnknown* object);

    void retire_shared(IUnknown* object);

    void collect();

    [[nodiscard]] bool close(std::string& error);

private:
    [[nodiscard]] bool drain(std::string& error);

    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    D3D12_COMMAND_LIST_TYPE type_ = D3D12_COMMAND_LIST_TYPE_DIRECT;
    std::uint64_t value_ = 0;
    bool dirty_ = false;
    std::vector<std::pair<std::uint64_t, IUnknown*>> retired_;
    std::vector<IUnknown*> recorded_;
    std::vector<ID3D12Fence*> external_;
};
}  // namespace lsw::suites
