#include "support/gl_identity.hpp"

#include "support/gl_enums.hpp"
#include "support/target_adapter.hpp"

#include <cctype>
#include <cstdio>

namespace lsw::suites {

namespace {

bool all_zero(const unsigned char* bytes) {
    for (int i = 0; i < 16; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

void erase_all(std::string& text, const std::string& needle) {
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle)) {
        text.erase(at, needle.size());
    }
}

}  // namespace

std::string GlIdentity::device_uuid(const Resolver& resolve, const ErrorDrain& drain_errors) {
    using GetBytev = void(__stdcall*)(unsigned, unsigned char*);
    auto* const bytev = reinterpret_cast<GetBytev>(resolve("glGetUnsignedBytevEXT"));
    if (bytev == nullptr) {
        return {};
    }
    if (drain_errors) {
        drain_errors();
    }
    unsigned char uuid[16] = {};
    bytev(glc::DEVICE_UUID_EXT, uuid);
    if ((drain_errors && drain_errors() != 0) || all_zero(uuid)) {
        return {};
    }
    std::string out;
    char text[4];
    for (unsigned char byte : uuid) {
        std::snprintf(text, sizeof text, "%02x", byte);
        out += text;
    }
    return out;
}

std::string GlIdentity::normalized(const std::string& name) {
    std::string text;
    for (char c : name) {
        text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    erase_all(text, "(r)");
    erase_all(text, "(tm)");
    for (std::size_t open = text.find('('); open != std::string::npos; open = text.find('(')) {
        const std::size_t close = text.find(')', open);
        text.erase(open, close == std::string::npos ? std::string::npos : close - open + 1);
    }
    std::string out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty() && word != "mesa") {
            out += out.empty() ? "" : " ";
            out += word;
        }
        word.clear();
    };
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            word += c;
        } else {
            flush();
        }
    }
    flush();
    return out;
}

bool GlIdentity::names_match(const std::string& renderer, const std::string& adapter_name) {
    const std::string a = normalized(renderer);
    const std::string b = normalized(adapter_name);
    return !a.empty() && !b.empty() && (a.find(b) != std::string::npos || b.find(a) != std::string::npos);
}

bool GlIdentity::matches(const AvAdapter& adapter, const std::string& gl_uuid, const std::string& renderer, std::string& how) {
    if (!gl_uuid.empty()) {
        const std::string vulkan = TargetAdapter::vulkan_uuid(adapter);
        if (!vulkan.empty()) {
            how = "GL_DEVICE_UUID_EXT " + gl_uuid + (gl_uuid == vulkan ? " equals" : " differs from") + " the Vulkan device UUID " + vulkan;
            return gl_uuid == vulkan;
        }
    }
    how = "no device UUID pair, GL_RENDERER '" + renderer + "' against adapter name '" + adapter.name + "'";
    return names_match(renderer, adapter.name);
}

}  // namespace lsw::suites
