#include "lsw/agent/uia/tree_dumper.hpp"

#include "lsw/agent/uia/uia_window.hpp"
#include "lsw/agent/win/bstr.hpp"

#include <utility>

namespace lsw::agent::uia {

namespace {

void dump_node(IUIAutomationTreeWalker* walker, IUIAutomationElement* element, int depth,
               int max_depth, nlohmann::json& out) {
    if (element == nullptr || depth > max_depth) {
        return;
    }
    CONTROLTYPEID control_type = 0;
    element->get_CurrentControlType(&control_type);
    win::Bstr name;
    win::Bstr automation_id;
    element->get_CurrentName(name.put());
    element->get_CurrentAutomationId(automation_id.put());
    RECT rect{};
    element->get_CurrentBoundingRectangle(&rect);

    nlohmann::json node = {
        {"control_type", static_cast<int>(control_type)},
        {"name", name.utf8()},
        {"automation_id", automation_id.utf8()},
        {"rect", {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top}},
    };

    nlohmann::json children = nlohmann::json::array();
    if (depth < max_depth) {
        win::ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement(element, child.GetAddressOf());
        while (child) {
            nlohmann::json child_json;
            dump_node(walker, child.Get(), depth + 1, max_depth, child_json);
            children.push_back(std::move(child_json));
            win::ComPtr<IUIAutomationElement> sibling;
            walker->GetNextSiblingElement(child.Get(), sibling.GetAddressOf());
            child = std::move(sibling);
        }
    }
    node["children"] = std::move(children);
    out = std::move(node);
}

}  // namespace

nlohmann::json TreeDumper::dump(const UiaWindow& window, int max_depth) {
    nlohmann::json out = nlohmann::json::object();
    if (!window.valid()) {
        return out;
    }
    win::ComPtr<IUIAutomationTreeWalker> walker;
    window.automation()->get_ControlViewWalker(walker.GetAddressOf());
    if (!walker) {
        return out;
    }
    dump_node(walker.Get(), window.element(), 0, max_depth > 0 ? max_depth : 4, out);
    return out;
}

}  // namespace lsw::agent::uia
