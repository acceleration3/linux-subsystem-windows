#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/pipeline_inputs.hpp"
#include "lsw/host/provision/pipeline_step.hpp"
#include "lsw/host/provision/setup_stage.hpp"

#include <QElapsedTimer>
#include <QObject>

#include <optional>
#include <QString>
#include <QUuid>
#include <QVector>

namespace lsw::host::services { class HostServices; }
namespace lsw::host::vm { struct DomainXmlInputs; }

class QTimer;

namespace lsw::host::provision {

namespace iso { class Downloader; }
namespace install { class PostInstallWatcher; }
class DebloatStager;
class PayloadIsoStager;
class SetupStageRunner;
class VncPortResolver;

class Pipeline : public QObject {
    Q_OBJECT
public:
    using Step = PipelineStep;

    explicit Pipeline(services::HostServices& services, QObject* parent = nullptr);
    ~Pipeline() override;

    void set_inputs(PipelineInputs in);
    [[nodiscard]] const PipelineInputs& inputs() const noexcept { return inputs_; }

    void start();

    void cancel();

    [[nodiscard]] const config::VmRecord& vm_record() const noexcept { return record_; }

    [[nodiscard]] const QVector<SetupStage>& setup_plan() const noexcept { return plan_; }

    [[nodiscard]] std::optional<Step> current_step() const noexcept { return current_step_; }
    [[nodiscard]] const QString& current_description() const noexcept {
        return current_description_;
    }
    [[nodiscard]] int vnc_port() const noexcept { return vnc_port_; }

Q_SIGNALS:
    void step_started(Step step, QString description);
    void step_progress(Step step, double fraction, QString detail);
    void step_finished(Step step);
    void failed(Step step, QString error);
    void completed(QUuid vm_uuid);

    void vnc_port_available(int port);

    void setup_plan_ready(QVector<SetupStage> stages);

    void setup_step_started(int row);
    void setup_step_progress(int row, QString detail);
    void setup_step_finished(int row, bool ok, QString detail);

    void payload_step_started(int row);
    void payload_step_progress(int row, QString detail);
    void payload_step_finished(int row, bool ok, QString detail);

private:
    void run_download();
    void run_trim_image();
    void run_build_payload();
    void run_create_disk();
    void replace_machine();
    void run_start_vm();
    void run_wait_for_agent();
    void run_setup_phase();
    void run_restart_vm();
    void poll_shutdown();
    void run_start_steady_vm();
    void run_wait_for_agent_after_restart();
    void finish();

    [[nodiscard]] vm::DomainXmlInputs domain_inputs(bool install_display) const;

    void emit_failed(Step step, const QString& error);
    void emit_completed(const QUuid& uuid);

    void emit_status(Step step, const QString& detail);

    services::HostServices& services_;
    PipelineInputs inputs_;
    config::VmRecord record_;

    bool started_ = false;
    bool cancelled_ = false;
    quint64 generation_ = 0;

    iso::Downloader* downloader_ = nullptr;
    DebloatStager* debloat_stager_ = nullptr;
    PayloadIsoStager* payload_stager_ = nullptr;
    install::PostInstallWatcher* agent_watcher_ = nullptr;
    VncPortResolver* vnc_resolver_ = nullptr;
    SetupStageRunner* setup_runner_ = nullptr;

    QTimer* shutdown_poll_ = nullptr;
    QElapsedTimer shutdown_clock_;
    QElapsedTimer step_clock_;
    int shutdown_ticks_ = 0;
    QTimer* restart_deadline_ = nullptr;

    QVector<SetupStage> plan_;
    std::optional<Step> current_step_;
    QString current_description_;
    int vnc_port_ = -1;
};

}
