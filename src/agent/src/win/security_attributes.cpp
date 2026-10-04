#include "lsw/agent/win/security_attributes.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"

// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

namespace lsw::agent::win {

SecurityAttributes::SecurityAttributes(const std::wstring& sddl) {
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                               &descriptor_, nullptr) == FALSE) {
        LSW_AGENT_ERROR("security: SDDL '{}' did not parse err={}", sddl, ErrorText::last_error());
        descriptor_ = nullptr;
        return;
    }
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_;
    attributes_.bInheritHandle = FALSE;
}

SecurityAttributes::~SecurityAttributes() {
    if (descriptor_ != nullptr) {
        ::LocalFree(descriptor_);
    }
}

}  // namespace lsw::agent::win
