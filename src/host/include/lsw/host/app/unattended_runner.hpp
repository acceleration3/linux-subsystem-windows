#pragma once

#include "lsw/host/app/cli_options.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::services { class HostServices; }

namespace lsw::host::app {

// The headless (``--unattended``) front end for the provisioning
// pipeline: turns CliOptions into PipelineInputs, prints one line per
// step transition to stdout, and exits the process with 0 on success or
// non-zero on failure.
//
// This is a *presenter*: exactly the role the wizard's QWizardPages play
// for interactive runs. It owns no provisioning logic of its own — every
// decision it reports comes from the pipeline.
class UnattendedRunner : public QObject {
    Q_OBJECT
public:
    UnattendedRunner(services::HostServices& services, CliOptions cli,
                     QObject* parent = nullptr);

    // Validate the options and start the pipeline. On a rejected
    // invocation this schedules a non-zero QCoreApplication::exit and
    // returns without starting anything.
    void start();

Q_SIGNALS:
    // Emitted with the process exit code the runner wants. The App turns
    // this into a QCoreApplication::exit.
    void finished(int exit_code);

private:
    // Reject the run with a message on stderr and ``code`` as the exit
    // status. Always deferred to the event loop — ``start`` may be called
    // before ``exec``.
    void fail(const QString& message, int code);

    // Print the pipeline's progress signals as one line each.
    void connect_progress_output();

    // Local-ISO path: list the WIM editions and resolve ``--edition``
    // (a 1-based index or a case-insensitive name substring) to an image
    // index. Returns false if the ISO could not be read or the label
    // matched nothing — the failure is already reported.
    [[nodiscard]] bool resolve_local_iso(provision::PipelineInputs& in);

    // Microsoft-download path: resolve edition + language to a URL, then
    // start the pipeline once it lands.
    void resolve_microsoft_iso(provision::PipelineInputs in);

    // Human-readable label for a coarse pipeline step.
    [[nodiscard]] static QString step_label(provision::Pipeline::Step s);
    static void println(const QString& line);

    services::HostServices& services_;
    CliOptions cli_;
    provision::Pipeline* pipeline_ = nullptr;  // child QObject
};

}  // namespace lsw::host::app
