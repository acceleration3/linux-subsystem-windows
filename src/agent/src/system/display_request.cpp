#include "lsw/agent/system/display_request.hpp"

#include <nlohmann/json.hpp>

namespace lsw::agent::system {

namespace {

int whole(const nlohmann::json& params, const char* name) {
    const auto found = params.find(name);
    return found != params.end() && found->is_number_integer() ? found->get<int>() : 0;
}

}  // namespace

DisplayRequest::DisplayRequest(const nlohmann::json& params) {
    if (!params.is_object()) {
        return;
    }
    scaling_percent_ = whole(params, "scaling_percent");
    const Resolution asked{whole(params, "width"), whole(params, "height")};
    if (asked.width > 0 && asked.height > 0) {
        resolution_ = asked;
    }
}

void DisplayRequest::answer(nlohmann::json& reply, const std::optional<Resolution>& now) {
    if (now) {
        reply["width"] = now->width;
        reply["height"] = now->height;
    }
}

}  // namespace lsw::agent::system
