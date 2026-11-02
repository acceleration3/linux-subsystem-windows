#pragma once

struct IUIAutomation;

namespace lsw::agent::uia {
class UiaRoot {
public:
    UiaRoot();
    ~UiaRoot();

    UiaRoot(const UiaRoot&) = delete;
    UiaRoot& operator=(const UiaRoot&) = delete;

    [[nodiscard]] IUIAutomation* get() const noexcept { return root_; }

    void shutdown();

private:
    IUIAutomation* root_ = nullptr;
};

}
