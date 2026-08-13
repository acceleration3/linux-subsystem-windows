#include "lsw/agent/uia/tree_dumper.hpp"

#include "lsw/agent/uia/bstr_text.hpp"
#include "lsw/agent/uia/element_locator.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

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
    BSTR name = nullptr;
    BSTR automation_id = nullptr;
    element->get_CurrentName(&name);
    element->get_CurrentAutomationId(&automation_id);
    RECT rect{};
    element->get_CurrentBoundingRectangle(&rect);

    nlohmann::json node = {
        {"control_type", static_cast<int>(control_type)},
        {"name", BstrText::to_utf8(name)},
        {"automation_id", BstrText::to_utf8(automation_id)},
        {"rect", {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top}},
    };
    if (name != nullptr) ::SysFreeString(name);
    if (automation_id != nullptr) ::SysFreeString(automation_id);

    nlohmann::json children = nlohmann::json::array();
    if (depth < max_depth) {
        IUIAutomationElement* child = nullptr;
        walker->GetFirstChildElement(element, &child);
        while (child != nullptr) {
            nlohmann::json child_json;
            dump_node(walker, child, depth + 1, max_depth, child_json);
            children.push_back(std::move(child_json));
            IUIAutomationElement* sibling = nullptr;
            walker->GetNextSiblingElement(child, &sibling);
            child->Release();
            child = sibling;
        }
    }
    node["children"] = std::move(children);
    out = std::move(node);
}

}  // namespace

nlohmann::json TreeDumper::run(IUIAutomation* root, std::uintptr_t hwnd, int max_depth) {
    nlohmann::json out = nlohmann::json::object();
    IUIAutomationElement* element = ElementLocator::from_window(root, hwnd);
    if (element == nullptr) {
        return out;
    }
    IUIAutomationTreeWalker* walker = nullptr;
    root->get_ControlViewWalker(&walker);
    if (walker == nullptr) {
        element->Release();
        return out;
    }
    dump_node(walker, element, 0, max_depth > 0 ? max_depth : 4, out);
    walker->Release();
    element->Release();
    return out;
}

}  // namespace lsw::agent::uia
