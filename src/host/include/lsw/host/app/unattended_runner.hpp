#pragma once

#include "lsw/host/app/cli_options.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::services { class HostServices; }

namespace lsw::host::app {
class UnattendedRunner : public QObject {
    Q_OBJECT
public:
    UnattendedRunner(services::HostServices& services, CliOptions cli,
                     QObject* parent = nullptr);

    void start();

Q_SIGNALS:
    void finished(int exit_code);

private:
    void fail(const QString& message, int code);

    void connect_progress_output();

    [[nodiscard]] bool resolve_local_iso(provision::PipelineInputs& in);

    void resolve_microsoft_iso(provision::PipelineInputs in);

    [[nodiscard]] static QString step_label(provision::Pipeline::Step s);
    static void println(const QString& line);

    services::HostServices& services_;
    CliOptions cli_;
    provision::Pipeline* pipeline_ = nullptr;
};

}
