#pragma once

#include <QString>
#include <QStringList>

namespace lsw::manager {

// Parsed command-line options for the headless install runner. Each
// field is populated only if the corresponding flag was passed; an
// empty QString / negative int means "not set, fall back to interactive
// UI".
//
// If ``unattended()`` returns true, ``App::run()`` skips the GUI
// wizard entirely and drives ``manager::wizard::Pipeline`` directly,
// printing step transitions and progress to stdout. The pipeline is
// the same one the GUI wizard uses; the CLI is just a different
// consumer of its signals.
struct CliOptions {
    bool accept_license = false;
    QString iso_path;        // --iso PATH (mutually exclusive with edition)
    QString edition_label;   // --edition LABEL (substring match against Edition.display)
    QString language_label;  // --language LABEL (substring against MsSku.language)
    QString user_name;       // --user NAME
    QString user_password;   // --password PASS
    QString computer_name;   // --computer-name NAME (Windows hostname)
    int cpu_cores = -1;      // --cpu N
    int memory_mib = -1;     // --memory MIB
    int disk_gib = -1;       // --disk GIB
    // --launch <app-id>: invoked by the XDG launcher entries we drop
    // into ~/.local/share/applications. The second process forwards
    // this to the running manager via SingleInstance and exits; if no
    // manager is running, we self-host (start the VM, run the launch
    // RPC, then keep the manager up).
    QString launch_app_id;

    // True iff any wizard-driving flag was supplied. We treat this as
    // "the user asked us to skip past everything we can" — pages with
    // all required inputs auto-advance, the welcome and done pages
    // become drive-through, and the Microsoft download pipeline (if
    // selected) auto-picks its dropdowns once they populate.
    [[nodiscard]] bool unattended() const noexcept {
        return accept_license || !iso_path.isEmpty() || !edition_label.isEmpty()
            || !user_name.isEmpty() || !user_password.isEmpty()
            || !computer_name.isEmpty() || cpu_cores > 0
            || memory_mib > 0 || disk_gib > 0;
    }
};

// Parse ``QApplication::arguments()``. On ``--help`` or a malformed
// invocation, the parser writes to stderr and exits the process via
// QCommandLineParser::process — which is what we want for a CLI tool.
[[nodiscard]] CliOptions parse_cli(const QStringList& argv);

}  // namespace lsw::manager
