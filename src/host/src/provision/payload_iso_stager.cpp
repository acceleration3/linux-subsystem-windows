#include "lsw/host/provision/payload_iso_stager.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/platform/dolphin_places.hpp"
#include "lsw/host/provision/install/autounattend.hpp"
#include "lsw/host/provision/install/media_language.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"
#include "lsw/host/provision/install/product_keys.hpp"
#include "lsw/host/services/guest_bundle.hpp"

#include <QTimeZone>
#include <QtConcurrent>

#include <string>

#include <utility>

namespace lsw::host::provision {

PayloadIsoStager::PayloadIsoStager(config::Settings& settings,
                                   const config::ManagerSettings& manager_settings, QObject* parent)
    : QObject(parent), settings_(settings), manager_settings_(manager_settings), bundle_(settings) {}

void PayloadIsoStager::build(const config::VmRecord& record, const PipelineInputs& inputs) {
    install::AutounattendInputs answers;
    answers.computer_name = inputs.computer_name.isEmpty() ? record.name : inputs.computer_name;
    answers.user_name = inputs.user_name;
    answers.user_password = inputs.user_password;
    answers.image_index = inputs.image_index;
    if (inputs.edition_id.isEmpty()) {
        answers.product_key = install::ProductKeys::fallback();
    } else {
        answers.product_key = install::ProductKeys::for_edition(inputs.edition_id);
        if (answers.product_key.isEmpty()) {
            LSW_WARN("provision: no product key known for edition {}; Setup will ask for one",
                     inputs.edition_id.toStdString());
        }
    }
    LSW_INFO("provision: edition {}, product key {}",
             inputs.edition_id.isEmpty() ? std::string{"not read"} : inputs.edition_id.toStdString(),
             answers.product_key.isEmpty() ? "none" : "set");
    answers.locale = inputs.language;
    if (answers.locale.isEmpty()) {
        QString why;
        answers.locale = install::MediaLanguage::of(inputs.iso_local_path,
                                                    settings_.cache_dir(), &why)
                             .value(0);
        if (answers.locale.isEmpty()) {
            LSW_WARN("provision: the image's language could not be read ({}); answering en-US",
                     why.toStdString());
            answers.locale = QStringLiteral("en-US");
        }
    }
    LSW_INFO("provision: Windows in {}", answers.locale.toStdString());
    answers.timezone = QString::fromUtf8(
        QTimeZone::ianaIdToWindowsId(QTimeZone::systemTimeZoneId()));
    if (answers.timezone.isEmpty()) {
        LSW_WARN("provision: Windows has no name for the time zone {}; answering UTC",
                 QTimeZone::systemTimeZoneId().toStdString());
        answers.timezone = QStringLiteral("UTC");
    }
    LSW_INFO("provision: time zone {}", answers.timezone.toStdString());

    (void)platform::DolphinPlaces::register_windows_bookmark(inputs.user_name,
                                                             inputs.user_password);

    const auto mounts = services::GuestBundle::mounts_of(manager_settings_);
    auto* const once = new QObject(this);
    connect(&bundle_, &services::GuestBundle::built, once,
            [this, once, answers](const std::shared_ptr<const services::GuestBundle::Contents>& contents) mutable {
                once->deleteLater();
                if (!contents->error.isEmpty()) {
                    LSW_WARN("provision: the guest bundle failed: {}", contents->error.toStdString());
                    Q_EMIT failed(QStringLiteral("LSW's files for Windows could not be put together: %1. "
                                                 "The guest's only display adapter needs its driver, so "
                                                 "there is no installing without them.")
                                      .arg(contents->error));
                    return;
                }
                answers.release = QString::fromStdString(contents->release);
                answers.version = QString::fromStdString(contents->version);
                LSW_INFO("provision: installing release {} ({} files)", contents->release.substr(0, 16),
                         contents->files.size());
                install::PayloadInputs payload;
                payload.output_iso_path = settings_.vm_payload_iso_path();
                payload.autounattend_xml = install::Autounattend::render(answers);
                payload.bundle = contents;
                build_iso(std::move(payload));
            });
    bundle_.build(mounts);
}

void PayloadIsoStager::build_iso(install::PayloadInputs payload) {
    struct Outcome {
        bool ok = false;
        QString error;
    };

    LSW_DEBUG("provision: building the payload ISO at {}", payload.output_iso_path.toStdString());
    (void)QtConcurrent::run([payload] {
        Outcome outcome;
        outcome.ok = install::PayloadIsoBuilder::build(payload, &outcome.error);
        return outcome;
    }).then(this, [this, path = payload.output_iso_path](const Outcome& outcome) {
        if (!outcome.ok) {
            LSW_WARN("provision: the payload ISO failed: {}", outcome.error.toStdString());
            Q_EMIT failed(QStringLiteral("payload ISO build failed: %1").arg(outcome.error));
            return;
        }
        LSW_INFO("provision: payload ISO written to {}", path.toStdString());
        Q_EMIT finished(path);
    });
}

}
