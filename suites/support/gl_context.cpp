#include "support/gl_context.hpp"

#include <vector>

namespace lsw::suites {

namespace {
constexpr int kMajor = 0x2091;
constexpr int kMinor = 0x2092;
constexpr int kFlags = 0x2094;
constexpr int kProfile = 0x9126;
constexpr int kReset = 0x8256;
constexpr int kNoError = 0x31B3;
}  // namespace

bool GlContext::create(HDC dc, const GlContextRequest& request, HGLRC share, std::string& error) {
    destroy();
    create_error_ = 0;
    GlLibrary& library = GlLibrary::instance();
    if (!library.ready(error)) {
        return false;
    }
    const bool legacy_request = request.major == 0 && request.profile == 0 && request.flags == 0 &&
                                request.reset_strategy == 0 && request.no_error < 0;
    if (legacy_request) {
        context_ = library.create_context(dc);
        if (context_ == nullptr) {
            create_error_ = ::GetLastError();
            error = "wglCreateContext failed (error " + std::to_string(create_error_) + ")";
            return false;
        }
        legacy_ = true;
        if (share != nullptr && library.share_lists(share, context_) == FALSE) {
            create_error_ = ::GetLastError();
            error = "wglShareLists failed (error " + std::to_string(create_error_) + ")";
            library.delete_context(context_);
            context_ = nullptr;
            return false;
        }
        return true;
    }
    HGLRC previous = library.current_context();
    HDC previous_dc = library.current_dc();
    HGLRC bootstrap = library.create_context(dc);
    if (bootstrap == nullptr || library.make_current(dc, bootstrap) == FALSE) {
        create_error_ = ::GetLastError();
        error = "the bootstrap legacy context for wglCreateContextAttribsARB failed (error " + std::to_string(create_error_) + ")";
        if (bootstrap != nullptr) {
            library.delete_context(bootstrap);
        }
        library.make_current(previous_dc, previous);
        return false;
    }
    WglApi wgl;
    wgl.load(library);
    if (wgl.create_context_attribs == nullptr) {
        error = "wglCreateContextAttribsARB is not exposed";
        library.make_current(previous_dc, previous);
        library.delete_context(bootstrap);
        return false;
    }
    std::vector<int> attributes;
    auto add = [&attributes](int key, int value) {
        attributes.push_back(key);
        attributes.push_back(value);
    };
    if (request.major != 0) {
        add(kMajor, request.major);
        add(kMinor, request.minor);
    }
    if (request.profile != 0) {
        add(kProfile, request.profile);
    }
    if (request.flags != 0) {
        add(kFlags, request.flags);
    }
    if (request.reset_strategy != 0) {
        add(kReset, request.reset_strategy);
    }
    if (request.no_error >= 0) {
        add(kNoError, request.no_error);
    }
    attributes.push_back(0);
    ::SetLastError(0);
    context_ = wgl.create_context_attribs(dc, share, attributes.data());
    create_error_ = ::GetLastError();
    library.make_current(previous_dc, previous);
    library.delete_context(bootstrap);
    if (context_ == nullptr) {
        error = "wglCreateContextAttribsARB failed (error " + std::to_string(create_error_) + ")";
        return false;
    }
    legacy_ = false;
    return true;
}

bool GlContext::make_current(HDC dc) const {
    return GlLibrary::instance().make_current(dc, context_) != FALSE;
}

void GlContext::release() {
    GlLibrary& library = GlLibrary::instance();
    library.make_current(nullptr, nullptr);
}

void GlContext::destroy() {
    if (context_ == nullptr) {
        return;
    }
    GlLibrary& library = GlLibrary::instance();
    if (library.current_context() == context_) {
        library.make_current(nullptr, nullptr);
    }
    library.delete_context(context_);
    context_ = nullptr;
}

}  // namespace lsw::suites
