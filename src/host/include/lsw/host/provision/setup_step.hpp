#pragma once

#include <QString>

namespace lsw::host::provision {
enum class StepKind { DownloadInstall, RunScript, DownloadExtract };

struct SetupStep {
    QString name;
    StepKind kind = StepKind::DownloadInstall;
    QString url;
    QString args;
    QString script;
    QString extract_to;
};

}
