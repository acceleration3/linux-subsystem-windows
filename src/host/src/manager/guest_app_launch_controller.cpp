#include "lsw/host/manager/guest_app_launch_controller.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/apps/guest_file_transfer.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/services/guest_upkeep.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/vm/graceful_shutdown.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/file_name.hpp"

#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>

namespace lsw::host::manager {

namespace {

// How long a guest may take to answer before the splash stops
// pretending this is normal: three minutes, or three times the last
// measured boot if that is longer. A slow boot is not a stuck one.
constexpr int kStallMs = 180000;
// How long the shared folders are given to be mounted once the agent
// answers; a guest whose setup never finishes would otherwise hold the
// documents for good.
constexpr int kSharesMs = 90000;

int stall_ms(std::optional<qint64> last_boot_ms) {
    return static_cast<int>(qMax<qint64>(kStallMs, 3 * last_boot_ms.value_or(0)));
}

// A store application's ID is its package family, '!', and the
// application's name; a Windows path always has a backslash.
bool is_store_target(const QString& target) {
    return target.contains(QLatin1Char('!')) && !target.contains(QLatin1Char('\\'));
}

// The file name of a Windows path or a host path.
QString file_name_of(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QFileInfo(path).fileName();
}

// What Windows chose to open one document with.
struct Handler {
    QString target;
    // Arguments that go before the document (``-jar``).
    QString args;
    QString error;
};

}  // namespace

GuestAppLaunchController::GuestAppLaunchController(services::HostServices& services,
                                                   QObject* parent)
    : QObject(parent), services_(services), launcher_(&services.app_launcher()) {
    connect(launcher_, &apps::GuestAppLauncher::launched, this,
            &GuestAppLaunchController::application_running);
    connect(launcher_, &apps::GuestAppLauncher::launched, this,
            [this](const QString& path, quint32) {
                if (splash_ && (splash_path_ == path || splash_path_.isEmpty()))
                    splash_->reach(LaunchSplash::Phase::ApplicationRunning);
            });
    connect(launcher_, &apps::GuestAppLauncher::failed, this,
            [this](const QString& path, const QString& error, const QStringList& documents) {
                // Always a failure splash of its own, with a retry that carries
                // what the launch was asked to open: the boot splash's retry is
                // about the wait, which is over by now, and would do nothing.
                show_failure(path, error, request_for(path, documents));
            });
}

GuestAppLaunchController::Request
GuestAppLaunchController::request_for(const QString& path, const QStringList& documents) const {
    Request request;
    request.target = path;
    request.documents = documents;
    for (const auto& app : services_.app_configs().configs()) {
        if (app.target() == path) {
            request.app_id = app.id;
            break;
        }
    }
    return request;
}

void GuestAppLaunchController::launch(const QString& app_id, const QString& activation_token) {
    Request request;
    request.app_id = app_id;
    request.activation_token = activation_token;
    open(std::move(request));
}

void GuestAppLaunchController::fail(const Request& request, const QString& reason) {
    show_failure(request.app_id.isEmpty() ? (request.documents.isEmpty() ? request.uploads.value(0)
                                                                         : request.documents.value(0))
                                          : request.app_id,
                 reason, request);
}

void GuestAppLaunchController::open(Request request) {
    const config::VmRecord* vm = services_.vms().primary();
    if (vm == nullptr) {
        LSW_WARN("launch_app({}): no VM defined", request.app_id.toStdString());
        show_failure(request.app_id.isEmpty() ? request.documents.value(0) : request.app_id,
                     QStringLiteral("There is no Windows machine set up yet, so there is nothing to "
                                    "open this in. Open the manager to set one up."),
                     request);
        return;
    }
    QString target;
    if (!request.app_id.isEmpty()) {
        // Resolve AppConfig id → guest Windows path. The .desktop entries
        // ship the stable id (sha1-prefix of path) in their ``Exec=``
        // because that is robust to spaces/backslashes and survives the app
        // moving on the guest side. The agent's launch RPC needs the path.
        const auto* cfg = services_.app_configs().find(request.app_id);
        if (cfg != nullptr && !cfg->target().isEmpty()) {
            target = cfg->target();
        } else if (request.has_documents()) {
            // An entry left in a file manager's Open With for an application
            // that is gone from the list: the documents are still wanted, and
            // Windows has a program for them.
            LSW_WARN("launch_app({}): no matching config; opening its documents with Windows' default",
                     request.app_id.toStdString());
            request.app_id.clear();
        } else {
            LSW_WARN("launch_app({}): no matching config (was the manager "
                     "ever run with this app installed in the guest?)",
                     request.app_id.toStdString());
            show_failure(request.app_id,
                         QStringLiteral("LSW does not have this application any more. Refresh the "
                                        "application list in the manager, or remove its entry from "
                                        "your menu."),
                         request);
            return;
        }
    }
    if (request.app_id.isEmpty()) {
        if (!request.target.isEmpty()) {
            target = request.target;
        } else if (!request.has_documents()) {
            return;
        }
        // Without a target it is not known until Windows says what opens the
        // first document; the splash goes by the file's name meanwhile.
    }
    const QUuid uuid = vm->uuid;

    // Intentionally do NOT show the main window — the user clicked an
    // XDG launcher to open a guest app, not to bring up the manager.
    ensure_channel_target(uuid);

    // The hub watches one VM; with no manager window open nobody has
    // pointed it at this one yet.
    services_.status().set_target(uuid);
    const bool restart = request.restart_from_id >= 0;

    if (waiting_active_) {
        // A boot is being waited for: this request waits with it. A plain
        // launch replaces the plain ones before it, as it always has; a
        // request with documents replaces nothing, because every file asked
        // for is to be opened.
        if (!request.has_documents()) {
            waiting_.removeIf([](const Waiting& w) { return !w.request.has_documents(); });
        }
        if (restart) {
            restart_from_id_ = request.restart_from_id;
        }
        waiting_.push_back({std::move(request), target});
        return;
    }
    if (!restart && services_.status().vm().agent_online()) {
        LSW_INFO("launch_app({}): firing immediately path={}", request.app_id.toStdString(),
                 target.toStdString());
        fire(request, target);
        return;
    }
    restart_from_id_ = restart ? request.restart_from_id : -1;
    start_vm_and_wait(uuid, Waiting{std::move(request), target});
}

void GuestAppLaunchController::fire(const Request& request, const QString& target) {
    if (request.has_documents()) {
        fire_documents(request, target);
    } else {
        launch_application(request, target);
    }
}

void GuestAppLaunchController::launch_application(const Request& request, const QString& target) {
    // Looked up again rather than carried over from the request: the
    // guest has been booting for a while, and the config may have been
    // edited in the meantime. One removed since launches its path bare.
    QString token = request.activation_token;
    if (const auto* cfg = services_.app_configs().find(request.app_id); cfg != nullptr) {
        launcher_->launch(*cfg, std::move(token));
        return;
    }
    apps::AppConfig bare;
    (is_store_target(target) ? bare.aumid : bare.path) = target;
    launcher_->launch(bare, std::move(token));
}

bool GuestAppLaunchController::shares_ready_for(const Request& request, const QString& target) {
    auto& upkeep = services_.upkeep();
    using State = services::GuestUpkeep::State;
    const QString shown = target.isEmpty() ? request.documents.value(0) : target;
    if (upkeep.shares_ready()) {
        return true;
    }
    // A pass that ended without getting to the shared folders (the update
    // failed) is not a guest that has them.
    if (upkeep.state() == State::Failed) {
        show_failure(shown,
                     QStringLiteral("Windows' shared folders were not set up: %1").arg(upkeep.error()),
                     request);
        return false;
    }
    // Still going: Checking and Updating run to the pass's own limit, with
    // the phase on the splash; only an upkeep that has not started at all is
    // given up on, after a while. A Cancel on the splash stops the wait.
    if (splash_) {
        splash_->say(upkeep.state() == State::Updating
                         ? QStringLiteral("Updating LSW's files in Windows")
                     : upkeep.state() == State::Checking
                         ? QStringLiteral("Checking LSW's files in Windows")
                         : QStringLiteral("Waiting for Windows to finish starting"));
    }
    auto* waiter = new QObject(this);
    upkeep_waiters_.push_back(waiter);
    if (upkeep.state() == State::Idle) {
        auto* patience = new QTimer(waiter);
        patience->setSingleShot(true);
        connect(patience, &QTimer::timeout, waiter, [this, waiter, request, shown] {
            waiter->deleteLater();
            show_failure(shown,
                         QStringLiteral("The shared folders were not ready in Windows. Open the log "
                                        "for details, then try again."),
                         request);
        });
        patience->start(kSharesMs);
    }
    connect(&upkeep, &services::GuestUpkeep::changed, waiter, [this, waiter, request, target] {
        waiter->deleteLater();
        fire_documents(request, target);
    });
    return false;
}

void GuestAppLaunchController::fire_documents(const Request& request, const QString& target) {
    if (!request.documents.isEmpty() && !shares_ready_for(request, target)) {
        return;
    }
    upload_documents(request, request, target, request.uploads, {}, {});
}

void GuestAppLaunchController::upload_documents(const Request& original, Request working,
                                                const QString& target, QStringList remaining,
                                                QStringList failed, QStringList failed_files) {
    if (remaining.isEmpty()) {
        working.uploads.clear();
        resolve_handlers(working, target, failed, failed_files);
        return;
    }
    const QString file = remaining.takeFirst();
    auto* transfer = new apps::GuestFileTransfer(services_.rpc(), this);
    connect(transfer, &apps::GuestFileTransfer::transferred, this,
            [this, transfer, original, working, target, remaining, failed, failed_files](const QString& guest_path) mutable {
                transfer->deleteLater();
                working.documents << guest_path;
                upload_documents(original, std::move(working), target, remaining, failed, failed_files);
            });
    connect(transfer, &apps::GuestFileTransfer::failed, this,
            [this, transfer, original, working, target, remaining, failed, failed_files, file](const QString& error) mutable {
                transfer->deleteLater();
                // The others still go: this one is reported at the end.
                failed << QStringLiteral("%1 could not be copied to Windows: %2")
                              .arg(QFileInfo(file).fileName(), error);
                failed_files << file;
                upload_documents(original, std::move(working), target, remaining, failed, failed_files);
            });
    // Called by a name Windows can create, whatever it is called here; the
    // copy is read-only, since what is written to it would only stay there.
    const QString name = QString::fromStdString(
        linkproto::WindowsFileName::sanitised(QFileInfo(file).fileName().toStdString()));
    transfer->start(file, name, /*document=*/true);
}

void GuestAppLaunchController::resolve_handlers(const Request& request, const QString& target,
                                                const QStringList& upload_failures,
                                                const QStringList& failed_files) {
    // One failure splash for everything that did not go: the copies that
    // failed and the documents Windows has no program for. Its retry is of
    // those alone.
    const auto report = [this, upload_failures, failed_files, request, target](
                            const QStringList& more, const QStringList& more_documents) {
        QStringList lines = upload_failures;
        lines << more;
        if (lines.isEmpty()) {
            return;
        }
        Request again = request;
        again.documents = more_documents;
        again.uploads = failed_files;
        again.target.clear();
        again.leading_args.clear();
        if (again.app_id.isEmpty()) {
            again.target = request.target;
        }
        show_failure(target.isEmpty() ? (failed_files.isEmpty() ? more_documents.value(0) : failed_files.value(0))
                                      : target,
                     lines.join(QLatin1Char('\n')), again);
    };
    // Nothing is left to open: only failures.
    if (request.documents.isEmpty()) {
        report({}, {});
        return;
    }
    // A program already chosen: the configured application, or one a
    // retry remembers.
    if (!request.app_id.isEmpty() || !request.target.isEmpty()) {
        launch_group(request, target);
        report({}, {});
        return;
    }
    // Windows' default for each document's type, asked in the user's
    // session, since the answer is the user's own choice of default.
    auto& rpc = services_.rpc();
    const QStringList documents = request.documents;
    QtConcurrent::run([&rpc, documents] {
        QVector<Handler> found;
        for (const QString& document : documents) {
            const auto reply = rpc.request(linkproto::cmd::kFileHandler,
                                           {{"path", document.toStdString()}});
            Handler handler;
            if (!reply.ok) {
                const QString extension = QFileInfo(file_name_of(document)).suffix();
                handler.error = QStringLiteral("Windows has no program for %1 files: %2")
                                    .arg(extension.isEmpty() ? file_name_of(document)
                                                             : QStringLiteral(".") + extension,
                                         QString::fromStdString(reply.error));
            } else {
                handler.target = QString::fromStdString(
                    reply.body.value("aumid", reply.body.value("exe_path", std::string{})));
                handler.args = QString::fromStdString(reply.body.value("args", std::string{}));
                if (handler.target.isEmpty()) {
                    handler.error = QStringLiteral("Windows named no program for %1")
                                        .arg(file_name_of(document));
                }
            }
            found.push_back(std::move(handler));
        }
        return found;
    }).then(this, [this, request, report](const QVector<Handler>& found) {
        // Grouped by program and arguments, in the order the documents came,
        // so each is launched once with all of its documents.
        QStringList order;
        QHash<QString, QStringList> groups;
        QHash<QString, Handler> first;
        QStringList errors;
        QStringList failed_documents;
        for (qsizetype i = 0; i < found.size(); ++i) {
            if (!found.at(i).error.isEmpty()) {
                errors << found.at(i).error;
                failed_documents << request.documents.at(i);
                continue;
            }
            const QString key = found.at(i).target.toCaseFolded() + QLatin1Char('\n') + found.at(i).args;
            if (!groups.contains(key)) {
                order << key;
                first.insert(key, found.at(i));
            }
            groups[key] << request.documents.at(i);
        }
        for (const QString& key : std::as_const(order)) {
            Request group = request;
            group.documents = groups.value(key);
            group.target = first.value(key).target;
            group.leading_args = first.value(key).args;
            // A configured application with that target opens them, with its
            // own arguments and environment.
            for (const auto& cfg : services_.app_configs().configs()) {
                if (cfg.target().compare(group.target, Qt::CaseInsensitive) == 0) {
                    group.app_id = cfg.id;
                    break;
                }
            }
            launch_group(group, group.target);
        }
        // One splash for every file that has no program, and a retry of
        // those alone.
        report(errors, failed_documents);
    });
}

void GuestAppLaunchController::launch_group(const Request& request, const QString& target) {
    QString program = target;
    const apps::AppConfig* configured = nullptr;
    if (!request.app_id.isEmpty()) {
        configured = services_.app_configs().find(request.app_id);
        if (configured != nullptr)
            program = configured->target();
    }
    if (program.isEmpty())
        program = request.target;
    if (splash_path_.isEmpty() && splash_)
        splash_path_ = program;
    // What a document needs before it on the command line, ahead of the
    // application's own arguments.
    const auto with_leading = [&request](apps::AppConfig config) {
        if (!request.leading_args.isEmpty()) {
            config.cmdline = config.cmdline.trimmed().isEmpty()
                                 ? request.leading_args
                                 : request.leading_args + QLatin1Char(' ') + config.cmdline;
        }
        return config;
    };
    if (configured != nullptr) {
        launcher_->launch(with_leading(*configured), request.activation_token, request.documents);
        return;
    }
    apps::AppConfig bare;
    (is_store_target(program) ? bare.aumid : bare.path) = program;
    launcher_->launch(with_leading(bare), request.activation_token, request.documents);
}

void GuestAppLaunchController::describe(const Request& request, const QString& target,
                                        QString* name, QIcon* icon) const {
    const apps::AppConfig* config = nullptr;
    if (!request.app_id.isEmpty()) {
        config = services_.app_configs().find(request.app_id);
    }
    if (config == nullptr && !target.isEmpty()) {
        for (const auto& app : services_.app_configs().configs()) {
            if (app.target() == target) {
                config = &app;
                break;
            }
        }
    }
    if (config != nullptr && !config->name.isEmpty()) {
        *name = config->name;
    } else if (!request.documents.isEmpty()) {
        *name = file_name_of(request.documents.first());
    } else if (!request.uploads.isEmpty()) {
        *name = file_name_of(request.uploads.first());
    } else if (!target.isEmpty()) {
        *name = file_name_of(target);
    } else {
        *name = request.app_id;
    }
    if (config != nullptr) {
        if (const QImage image = config->icon_image(); !image.isNull()) {
            *icon = QIcon{QPixmap::fromImage(image)};
        }
    }
}

void GuestAppLaunchController::show_failure(const QString& target, const QString& error,
                                            const Request& request) {
    // Launched from the launcher into a guest already up, there was no
    // splash; the failure gets one of its own, in its failed state, since
    // with the manager's window closed there is nowhere else to say it.
    QString name = file_name_of(target);
    QIcon icon;
    describe(request, target, &name, &icon);
    if (splash_) splash_->close();
    auto* splash = new LaunchSplash(name, icon, services_.status().last_boot_ms());
    splash_ = splash;
    splash_path_ = target;
    splash->reach(LaunchSplash::Phase::GuestAnswering);
    connect(splash, &LaunchSplash::log_requested, this, &GuestAppLaunchController::log_requested);
    connect(splash, &LaunchSplash::retry_requested, this, [this, target, request] {
        // Fired again as it was asked, uploads included.
        fire(request, target);
    });
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        screen != nullptr || (screen = QGuiApplication::primaryScreen()) != nullptr) {
        splash->adjustSize();
        splash->move(screen->availableGeometry().center() - splash->rect().center());
    }
    splash->fail(error);
}

void GuestAppLaunchController::ensure_channel_target(const QUuid& uuid) {
    if (!services_.rpc().has_target()) {
        (void)services_.bind_agent(uuid);
    }
}

void GuestAppLaunchController::start_vm_and_wait(const QUuid& uuid, const Waiting& first) {
    if (splash_)
        splash_->close();
    waiting_.clear();
    waiting_.push_back(first);
    waiting_active_ = true;
    auto& lifecycle = services_.lifecycle();
    int start_error = 0;
    if (!lifecycle.is_running(uuid)) {
        if (const int rc = services_.start_vm(uuid); rc != 0) {
            start_error = rc;
            LSW_WARN("launch_app({}): lifecycle.start rc={}", first.request.app_id.toStdString(), rc);
        }
    }

    // Parentless top-level so the splash is not tied to the (possibly
    // hidden) main window, and so it gets its own taskbar slot. Centred
    // on whichever screen the cursor is on at the moment of the click.
    QString display_name = first.request.app_id;
    QIcon icon;
    describe(first.request, first.target, &display_name, &icon);

    auto* splash = new LaunchSplash(display_name, icon, services_.status().last_boot_ms());
    splash_ = splash;
    splash_path_ = first.target;
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        screen != nullptr || (screen = QGuiApplication::primaryScreen()) != nullptr) {
        splash->adjustSize();
        splash->move(screen->availableGeometry().center() - splash->rect().center());
    }
    QPointer<LaunchSplash> splash_ptr(splash);
    if (lifecycle.is_running(uuid) && restart_from_id_ < 0) {
        splash->reach(LaunchSplash::Phase::MachineStarted);
    }

    // A guest that has not answered in this long is not merely slow.
    // Rather than spin forever, say so and offer the two things that
    // help: the log, and a restart.
    auto* patience = new QTimer(splash);
    patience->setSingleShot(true);
    patience->setInterval(stall_ms(services_.status().last_boot_ms()));
    connect(patience, &QTimer::timeout, this, [splash_ptr] {
        if (splash_ptr) {
            splash_ptr->stall(QStringLiteral(
                "Usually a guest still finishing an update. The log will say if the agent "
                "crashed."));
        }
    });
    patience->start();

    // The status hub reads the machine every couple of seconds. Wait
    // for the agent to answer (or a cancel), then fire what waits.
    auto* wait = new QObject(splash);
    connect(&services_.status(), &services::StatusHub::changed, wait,
            [this, wait, uuid, splash_ptr, patience](services::StatusHub::Area area) {
                const auto& vm = services_.status().vm();
                if (area != services::StatusHub::Area::Vm || vm.uuid != uuid)
                    return;
                if (!waiting_active_)
                    return;  // cancelled / superseded
                // A restart is over when the old boot is seen to have gone;
                // until then neither its QEMU nor its agent is the one waited for.
                if (restart_from_id_ >= 0) {
                    // The boot that was ending is not the one waited for: the
                    // new one has another id, which is seen even when the
                    // restart was quicker than the readings.
                    if (!vm.running() || vm.domain_id < 0 || vm.domain_id == restart_from_id_)
                        return;
                }
                // The phases follow the hub's readings: QEMU up, then the agent.
                if (vm.running() && splash_ptr)
                    splash_ptr->reach(LaunchSplash::Phase::MachineStarted);
                if (!vm.agent_online())
                    return;
                waiting_active_ = false;
                restart_from_id_ = -1;
                patience->stop();
                if (splash_ptr)
                    splash_ptr->reach(LaunchSplash::Phase::GuestAnswering);
                const QVector<Waiting> ready = std::exchange(waiting_, {});
                wait->deleteLater();
                for (const Waiting& w : ready)
                    fire(w.request, w.target);
            });
    connect(splash, &LaunchSplash::cancelled, wait, [this, wait, patience] {
        waiting_.clear();
        waiting_active_ = false;
        restart_from_id_ = -1;
        patience->stop();
        wait->deleteLater();
        // A wait for the shared folders is part of the same request.
        for (const auto& waiter : std::exchange(upkeep_waiters_, {})) {
            delete waiter.data();
        }
    });
    connect(splash, &LaunchSplash::retry_requested, splash, [this] {
        if (services_.status().vm().agent_online() && restart_from_id_ < 0) {
            waiting_active_ = false;
            const QVector<Waiting> ready = std::exchange(waiting_, {});
            for (const Waiting& w : ready)
                fire(w.request, w.target);
            return;
        }
        // The launcher's token, if the launch never fired, is still the
        // one its first window is to be activated with: it rides in the
        // request, which is asked again from the start.
        waiting_active_ = false;
        const QVector<Waiting> again = std::exchange(waiting_, {});
        for (const Waiting& w : again)
            open(w.request);
    });
    connect(splash, &LaunchSplash::restart_requested, splash, [this, uuid, splash_ptr, patience] {
        // A start while the old QEMU still runs does nothing, so the start
        // waits for the stop, as the manager's own restart does.
        if (splash_ptr) {
            splash_ptr->resume();
        }
        patience->start();
        auto* restart = new vm::GracefulShutdown(services_.lifecycle(), uuid,
                                                 vm::GracefulShutdown::kDefaultDeadlineMs, this);
        connect(restart, &vm::GracefulShutdown::finished, this, [this, restart, uuid] {
            restart->deleteLater();
            (void)services_.start_vm(uuid);
        });
        restart->start();
    });
    connect(splash, &LaunchSplash::log_requested, this, &GuestAppLaunchController::log_requested);
    splash->show();
    if (start_error != 0) {
        patience->stop();
        wait->deleteLater();
        // What was asked for is kept: the splash's retry asks for it again.
        waiting_active_ = false;
        splash->fail(QStringLiteral("Windows could not start (error %1). Open the log for details, "
                                    "then try again.")
                         .arg(start_error));
    }
}

}  // namespace lsw::host::manager
