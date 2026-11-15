#include "support/dxc_compiler.hpp"

#include <cstdlib>

namespace lsw::suites {

DxcCompiler::~DxcCompiler() {
    if (compiler_ != nullptr) {
        compiler_->Release();
    }
    if (library_ != nullptr) {
        library_->Release();
    }
    if (module_ != nullptr) {
        ::FreeLibrary(module_);
    }
}

bool DxcCompiler::open(std::string& error) {
    const char* directory = std::getenv("LSW_SUITES_DXC_DIR");
    if (directory != nullptr) {
        const std::string path = std::string(directory) + "\\dxcompiler.dll";
        module_ = ::LoadLibraryA(path.c_str());
    }
    if (module_ == nullptr) {
        module_ = ::LoadLibraryW(L"dxcompiler.dll");
    }
    if (module_ == nullptr) {
        error = "dxcompiler.dll cannot be loaded (set LSW_SUITES_DXC_DIR to its folder)";
        return false;
    }
    using Create = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    const Create create = reinterpret_cast<Create>(reinterpret_cast<void*>(::GetProcAddress(module_, "DxcCreateInstance")));
    if (create == nullptr) {
        error = "dxcompiler.dll exports no DxcCreateInstance";
        return false;
    }
    if (FAILED(create(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&compiler_))) ||
        FAILED(create(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&library_)))) {
        error = "DxcCreateInstance failed";
        return false;
    }
    return true;
}

bool DxcCompiler::compile(const std::string& source, const wchar_t* entry, const wchar_t* target, std::vector<std::uint8_t>& code,
                          std::string& log) {
    IDxcBlobEncoding* text = nullptr;
    if (FAILED(library_->CreateBlobWithEncodingFromPinned(source.data(), static_cast<UINT32>(source.size()), CP_UTF8, &text))) {
        log = "creating the source blob failed";
        return false;
    }
    IDxcOperationResult* result = nullptr;
    const HRESULT submitted = compiler_->Compile(text, L"shader.hlsl", entry, target, nullptr, 0, nullptr, 0, nullptr, &result);
    text->Release();
    if (FAILED(submitted) || result == nullptr) {
        log = "IDxcCompiler::Compile failed";
        return false;
    }
    HRESULT status = S_OK;
    result->GetStatus(&status);
    IDxcBlobEncoding* errors = nullptr;
    if (SUCCEEDED(result->GetErrorBuffer(&errors)) && errors != nullptr) {
        if (errors->GetBufferSize() != 0) {
            log.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        }
        errors->Release();
    }
    bool ok = false;
    if (SUCCEEDED(status)) {
        IDxcBlob* blob = nullptr;
        if (SUCCEEDED(result->GetResult(&blob)) && blob != nullptr) {
            const auto* bytes = static_cast<const std::uint8_t*>(blob->GetBufferPointer());
            code.assign(bytes, bytes + blob->GetBufferSize());
            blob->Release();
            ok = !code.empty();
        }
    }
    result->Release();
    return ok;
}

}  // namespace lsw::suites
