#pragma once

#include "lsw/agent/apps/start_menu_watcher.hpp"
#include "lsw/agent/apps/upload_store.hpp"
#include "lsw/agent/console/console.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/win/com.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace lsw::agent::uia {
class Uia;
}

namespace lsw::agent::win {
class ClipboardWatcher;
}

namespace lsw::agent::session {
class SessionManager;
}

namespace lsw::agent::core {
class AgentServices {
public:
    enum class Role { Service, SessionHelper };

    explicit AgentServices(Role role);
    ~AgentServices();

    AgentServices(const AgentServices&) = delete;
    AgentServices& operator=(const AgentServices&) = delete;

    [[nodiscard]] Role role() const noexcept { return role_; }

    [[nodiscard]] install::Installer& installer() noexcept { return installer_; }

    [[nodiscard]] console::Console& console() noexcept { return console_; }
    [[nodiscard]] apps::UploadStore& uploads() noexcept { return uploads_; }
    [[nodiscard]] apps::StartMenuWatcher& start_menu() noexcept { return start_menu_; }

    [[nodiscard]] uia::Uia& uia();

    [[nodiscard]] win::ClipboardWatcher* clipboard() noexcept { return clipboard_.get(); }

    [[nodiscard]] session::SessionManager& sessions();

    void attach_sessions(std::unique_ptr<session::SessionManager> sessions);

    [[nodiscard]] std::string mirror_log_level() const;
    void set_mirror_log_level(std::string level);

    void shutdown();

private:
    Role role_;
    install::Installer installer_;
    console::Console console_;
    apps::UploadStore uploads_;
    apps::StartMenuWatcher start_menu_;
    win::Apartment apartment_;
    std::unique_ptr<uia::Uia> uia_;
    std::unique_ptr<win::ClipboardWatcher> clipboard_;
    std::unique_ptr<session::SessionManager> sessions_;
    mutable std::mutex mirror_level_mu_;
    std::string mirror_log_level_ = "info";
};

}
