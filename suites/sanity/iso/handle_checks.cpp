#include "sanity/iso/handle_checks.hpp"

#include "core/child_mode.hpp"
#include "core/child_protocol.hpp"
#include "core/text.hpp"
#include "sanity/iso/magic.hpp"
#include "support/api_policy.hpp"
#include "support/av_adapters.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/target_adapter.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace lsw::suites::sanity::iso {

namespace {

constexpr UINT64 kShared = 1ull << 20;

ID3D12Resource* shared_buffer(D3d12Rig& rig, std::string& error) {
    HRESULT result = S_OK;
    ID3D12Resource* buffer = D3d12Tools::committed(rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED, D3d12Tools::buffer_desc(kShared),
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
    if (buffer == nullptr) {
        error = "creating a shared buffer returned " + D3d12Tools::hr(result);
    }
    return buffer;
}

bool fill_shared(D3d12Rig& rig, ID3D12Resource* buffer, std::string& error) {
    std::vector<std::uint8_t> data(kShared);
    Magic::fill(data.data(), data.size(), 0);
    return D3d12Tools::write_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), data.size(), error);
}

Outcome guess_role(Context& context, const std::string& role) {
    if (role != "guess") {
        return Outcome::fail("unknown role " + role);
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail("D3D12 is unavailable: " + error);
    }
    const std::string parent = ChildMode::variable(L"LSW_SUITES_PARENT_PID");
    int opened = 0;
    int tried = 0;
    for (std::uintptr_t value = 4; value < 16384; value += 4) {
        ID3D12Resource* resource = nullptr;
        ++tried;
        if (SUCCEEDED(rig.device()->OpenSharedHandle(reinterpret_cast<HANDLE>(value), __uuidof(ID3D12Resource), reinterpret_cast<void**>(&resource)))) {
            ++opened;
            resource->Release();
        }
    }
    const char* patterns[] = {"Local\\lsw-suites-iso-handle-", "Local\\lsw-suites-d3d12-share-", "Global\\lsw-suites-iso-handle-", "lsw-suites-iso-handle-"};
    for (const char* pattern : patterns) {
        for (const std::string& suffix : {parent, std::string("0"), std::string("1"), std::string("4")}) {
            HANDLE handle = nullptr;
            ++tried;
            if (SUCCEEDED(rig.device()->OpenSharedHandleByName(Text::wide(std::string(pattern) + suffix).c_str(), GENERIC_ALL, &handle))) {
                ++opened;
                ::CloseHandle(handle);
            }
        }
    }
    context.measure("tried", tried);
    context.measure("opened", opened);
    return opened == 0 ? Outcome::ok() : Outcome::fail(std::to_string(opened) + " of " + std::to_string(tried) + " guessed handle values or names opened a shared resource");
}

Outcome closed_role(Context& context, const std::string& role) {
    if (role != "closed") {
        return Outcome::fail("unknown role " + role);
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail("D3D12 is unavailable: " + error);
    }
    HANDLE handle = nullptr;
    const HRESULT result = rig.device()->OpenSharedHandleByName(Text::wide(ChildMode::variable(L"LSW_SUITES_SHARE_NAME")).c_str(), GENERIC_ALL, &handle);
    if (SUCCEEDED(result)) {
        ::CloseHandle(handle);
        return Outcome::fail("opening the shared object by name succeeded after its owner and every handle were closed");
    }
    context.measure("open_after_close", D3d12Tools::hr(result));
    std::uint64_t stale = 0;
    for (UINT64 size : {kShared, 4 * kShared}) {
        ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COMMON);
        D3d12Tools::Bytes bytes;
        if (buffer == nullptr || !D3d12Tools::read_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, size, bytes, error)) {
            return Outcome::fail("reading a new buffer: " + error);
        }
        stale += Magic::count(bytes.data(), bytes.size());
    }
    context.measure("stale_words", static_cast<long long>(stale));
    return stale > 1 ? Outcome::fail(std::to_string(stale) + " words of the closed shared object appear in new allocations") : Outcome::ok();
}

Outcome session_role(Context& context, const std::string& role) {
    if (role != "session0") {
        return Outcome::fail("unknown role " + role);
    }
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
    context.measure("session", static_cast<long long>(session));
    std::vector<AvAdapter> adapters;
    std::string error;
    std::string keys;
    if (AvAdapters::accel(adapters, error)) {
        std::vector<std::uint64_t> sorted;
        for (const AvAdapter& adapter : adapters) {
            sorted.push_back(adapter.luid_key());
        }
        std::sort(sorted.begin(), sorted.end());
        for (std::uint64_t key : sorted) {
            keys += std::to_string(key) + ",";
        }
    }
    context.measure("adapters", keys);
    AvAdapter target;
    D3d12Rig rig;
    std::string state = "no device";
    if (TargetAdapter::pick(target, error) && rig.open_luid(target.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        HANDLE handle = nullptr;
        const HRESULT result = rig.device()->OpenSharedHandleByName(Text::wide(ChildMode::variable(L"LSW_SUITES_SHARE_NAME")).c_str(), GENERIC_ALL, &handle);
        state = SUCCEEDED(result) ? "opened" : "refused " + D3d12Tools::hr(result);
        if (SUCCEEDED(result)) {
            ::CloseHandle(handle);
        }
    }
    context.measure("open_session1_name", state);
    return Outcome::ok();
}

bool run_hidden(const std::wstring& command, DWORD& code) {
    std::wstring line = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        return false;
    }
    ::WaitForSingleObject(info.hProcess, 60000);
    ::GetExitCodeProcess(info.hProcess, &code);
    ::CloseHandle(info.hProcess);
    ::CloseHandle(info.hThread);
    return true;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

}  // namespace

Outcome HandleChecks::no_access_without_handle(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, guess_role);
    }
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    ID3D12Resource* buffer = shared_buffer(rig, error);
    if (buffer == nullptr) {
        return Outcome::fail("a shared D3D12 buffer cannot be created: " + error);
    }
    HANDLE handle = nullptr;
    const HRESULT shared = rig.device()->CreateSharedHandle(buffer, nullptr, GENERIC_ALL, nullptr, &handle);
    if (FAILED(shared)) {
        return Outcome::fail("CreateSharedHandle of a shared buffer returned " + D3d12Tools::hr(shared));
    }
    if (!fill_shared(rig, buffer, error)) {
        return Outcome::fail("preparing the shared buffer failed: " + error);
    }
    const CheckResult child = RobustChild::run(context.id(), "guess", {{"LSW_SUITES_PARENT_PID", std::to_string(::GetCurrentProcessId())}}, 300, -1);
    RobustChild::forward(context, "child.", child);
    Problems problems;
    if (child.status != Status::Ok && child.status != Status::Skip) {
        problems.add("the guessing process: " + std::string(Outcome::name(child.status)) + " " + child.reason);
    }
    D3d12Tools::Bytes bytes;
    if (!D3d12Tools::read_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, kShared, bytes, error)) {
        problems.add(error);
    } else {
        std::vector<std::uint8_t> expected(kShared);
        Magic::fill(expected.data(), expected.size(), 0);
        if (std::memcmp(expected.data(), bytes.data(), bytes.size()) != 0) {
            problems.add("the shared buffer changed while another process guessed handles");
        }
    }
    ::CloseHandle(handle);
    return problems.outcome();
}

Outcome HandleChecks::closed_handle_dead(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, closed_role);
    }
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    const std::string name = "Local\\lsw-suites-iso-closed-" + std::to_string(::GetCurrentProcessId());
    {
        AvAdapter adapter;
        std::string error;
        D3d12Rig rig;
        if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            return Outcome::fail(error);
        }
        ID3D12Resource* buffer = shared_buffer(rig, error);
        if (buffer == nullptr) {
            return Outcome::fail("a shared D3D12 buffer cannot be created: " + error);
        }
        HANDLE handle = nullptr;
        const HRESULT shared = rig.device()->CreateSharedHandle(buffer, nullptr, GENERIC_ALL, Text::wide(name).c_str(), &handle);
        if (FAILED(shared)) {
            return Outcome::fail("CreateSharedHandle of a shared buffer returned " + D3d12Tools::hr(shared));
        }
        if (!fill_shared(rig, buffer, error)) {
            return Outcome::fail("preparing the shared buffer failed: " + error);
        }
        ::CloseHandle(handle);
        rig.release(buffer);
        if (!rig.idle(error)) {
            return Outcome::fail(error);
        }
    }
    const CheckResult child = RobustChild::run(context.id(), "closed", {{"LSW_SUITES_SHARE_NAME", name}}, 300, -1);
    RobustChild::forward(context, "child.", child);
    if (child.status == Status::Skip) {
        return Context::skip(child.reason);
    }
    return child.status == Status::Ok ? Outcome::ok() : Outcome::fail(child.reason);
}

Outcome HandleChecks::read_only_enforced(Context& context) {
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("d3d11 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device owner;
    D3d11Device opener;
    if (!TargetAdapter::pick(adapter, error) || !owner.create(adapter.luid, error) || !opener.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(owner, desc);
    Com<ID3D11RenderTargetView> view = texture ? D3d11Tools::rtv(owner, texture.get()) : Com<ID3D11RenderTargetView>();
    Com<IDXGIResource1> resource;
    if (texture) {
        texture->QueryInterface(__uuidof(IDXGIResource1), resource.put_void());
    }
    const std::string name = "Local\\lsw-suites-iso-readonly-" + std::to_string(::GetCurrentProcessId());
    HANDLE handle = nullptr;
    if (!view || !resource || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, Text::wide(name).c_str(), &handle))) {
        return Outcome::fail("a read-only NT-handle shared D3D11 texture cannot be created");
    }
    const float original[4] = {0.2f, 0.4f, 0.6f, 1.0f};
    owner.context->ClearRenderTargetView(view.get(), original);
    if (!D3d11Tools::wait_event(owner, owner.context, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Device1> opener1;
    opener.device->QueryInterface(__uuidof(ID3D11Device1), opener1.put_void());
    if (!opener1) {
        ::CloseHandle(handle);
        return Outcome::fail("ID3D11Device1 is not exposed, so shared handles cannot be opened by name");
    }
    Problems problems;
    Com<ID3D11Texture2D> writable;
    const HRESULT write_open = opener1->OpenSharedResourceByName(Text::wide(name).c_str(), DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), writable.put_void());
    context.measure("open_with_write_access", D3d11Tools::hr(write_open));
    Com<ID3D11Texture2D> readable;
    const HRESULT read_open = opener1->OpenSharedResourceByName(Text::wide(name).c_str(), DXGI_SHARED_RESOURCE_READ, __uuidof(ID3D11Texture2D), readable.put_void());
    context.measure("open_with_read_access", D3d11Tools::hr(read_open));
    if (FAILED(read_open)) {
        problems.add("opening with the granted read access failed: " + D3d11Tools::hr(read_open));
    }
    Com<ID3D11Texture2D> victim = SUCCEEDED(write_open) ? std::move(writable) : std::move(readable);
    if (victim) {
        std::vector<std::uint32_t> pixels(64 * 64, 0xff0000ffu);
        Com<ID3D11Texture2D> source = D3d11Tools::texture(opener, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE));
        opener.context->UpdateSubresource(victim.get(), 0, nullptr, pixels.data(), 64 * 4, 0);
        if (source) {
            opener.context->UpdateSubresource(source.get(), 0, nullptr, pixels.data(), 64 * 4, 0);
            opener.context->CopyResource(victim.get(), source.get());
        }
        Com<ID3D11RenderTargetView> foreign;
        const HRESULT created = opener.device->CreateRenderTargetView(victim.get(), nullptr, foreign.put());
        context.measure("rtv_on_read_only", D3d11Tools::hr(created));
        if (SUCCEEDED(created)) {
            const float other[4] = {1.0f, 0.0f, 0.0f, 1.0f};
            opener.context->ClearRenderTargetView(foreign.get(), other);
        }
        opener.context->Flush();
        static_cast<void>(D3d11Tools::wait_event(opener, opener.context, error));
    }
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(owner, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    TexelImage image;
    if (!staging) {
        return Outcome::fail("creating the staging texture failed");
    }
    owner.context->CopyResource(staging.get(), texture.get());
    if (!D3d11Tools::read_texture(owner, staging.get(), 0, image, error)) {
        return Outcome::fail(error);
    }
    const std::uint32_t expected = 51u | (102u << 8) | (153u << 16) | 0xff000000u;
    for (UINT y = 0; y < 64; y += 21) {
        for (UINT x = 0; x < 64; x += 21) {
            if (image.pixel(x, y) != expected) {
                problems.add("the owner's texture holds 0x" + Text::hex(image.pixel(x, y), 8) + " at (" + std::to_string(x) + "," + std::to_string(y) + ") after the read-only opener tried to write, expected 0x" + Text::hex(expected, 8));
                x = 64;
                y = 64;
            }
        }
    }
    ::CloseHandle(handle);
    return problems.outcome();
}

Outcome HandleChecks::service_session_separate(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, session_role);
    }
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    ID3D12Resource* buffer = shared_buffer(rig, error);
    HANDLE handle = nullptr;
    const std::string name = "Local\\lsw-suites-iso-session-" + std::to_string(::GetCurrentProcessId());
    if (buffer == nullptr || FAILED(rig.device()->CreateSharedHandle(buffer, nullptr, GENERIC_ALL, Text::wide(name).c_str(), &handle))) {
        return Outcome::fail("a shared D3D12 buffer cannot be created: " + error);
    }
    std::vector<AvAdapter> own;
    std::string own_keys;
    if (AvAdapters::accel(own, error)) {
        std::vector<std::uint64_t> sorted;
        for (const AvAdapter& item : own) {
            sorted.push_back(item.luid_key());
        }
        std::sort(sorted.begin(), sorted.end());
        for (std::uint64_t key : sorted) {
            own_keys += std::to_string(key) + ",";
        }
    }
    const std::filesystem::path root = std::filesystem::path(std::getenv("ProgramData") != nullptr ? std::getenv("ProgramData") : "C:\\ProgramData");
    const std::filesystem::path script = root / "lsw-suites-session0.cmd";
    const std::filesystem::path output = root / "lsw-suites-session0.out";
    std::error_code ignore;
    std::filesystem::remove(output, ignore);
    {
        std::ofstream file(script, std::ios::trunc);
        file << "@echo off\r\nset LSW_SUITES_CHILD=1\r\nset LSW_SUITES_ROLE=session0\r\nset LSW_SUITES_SHARE_NAME=" << name << "\r\n\""
             << ChildRuns::own_exe().string() << "\" --run-one " << context.id() << " --artifacts \"" << (root / "lsw-suites-session0-art").string() << "\" > \"" << output.string() << "\" 2>&1\r\n";
    }
    const std::string task = "lsw-suites-session0";
    DWORD code = 0;
    const std::wstring create = L"schtasks.exe /Create /TN " + Text::wide(task) + L" /TR \"" + script.wstring() + L"\" /SC ONCE /ST 23:59 /RU SYSTEM /RL HIGHEST /F";
    if (!run_hidden(create, code) || code != 0) {
        ::CloseHandle(handle);
        return Context::skip("a SYSTEM task for a session 0 process cannot be created (schtasks exit " + std::to_string(code) + ")");
    }
    const bool started = run_hidden(L"schtasks.exe /Run /TN " + Text::wide(task), code) && code == 0;
    std::string text;
    for (int i = 0; started && i < 240; ++i) {
        text = read_text(output);
        if (text.find("@@lsw status") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    run_hidden(L"schtasks.exe /Delete /TN " + Text::wide(task) + L" /F", code);
    ::CloseHandle(handle);
    CheckResult result;
    if (!started || !ChildProtocol::decode(text, result)) {
        return Context::skip("the session 0 process did not report (started " + std::string(started ? "yes" : "no") + ")");
    }
    RobustChild::forward(context, "session0.", result);
    if (result.status != Status::Ok) {
        return Outcome::fail("the session 0 process: " + std::string(Outcome::name(result.status)) + " " + result.reason);
    }
    Problems problems;
    if (ChildRuns::number(result, "session") != 0) {
        problems.add("the task did not run in session 0");
    }
    if (ChildRuns::text(result, "adapters") != own_keys) {
        problems.add("session 0 sees adapters [" + ChildRuns::text(result, "adapters") + "], the user session sees [" + own_keys + "]");
    }
    if (ChildRuns::text(result, "open_session1_name") == "opened") {
        problems.add("a process in session 0 opened a session-local shared resource of the user session");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::iso
