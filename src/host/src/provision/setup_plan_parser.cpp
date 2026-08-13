#include "lsw/host/provision/setup_plan_parser.hpp"

#include "lsw/base/log.hpp"
#include "scripts/tune_windows.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::provision {

namespace {

[[nodiscard]] bool parse_one_step(const nlohmann::json& s, SetupStep& step) {
    if (!s.is_object()) {
        return false;
    }
    step.name = QString::fromStdString(s.value("name", std::string{"(unnamed)"}));
    const auto kind = s.value("kind", std::string{});
    if (kind == "download_install") {
        step.kind = StepKind::DownloadInstall;
        step.url = QString::fromStdString(s.value("url", std::string{}));
        step.args = QString::fromStdString(s.value("args", std::string{}));
        return !step.url.isEmpty();
    }
    if (kind == "run_script") {
        step.kind = StepKind::RunScript;
        const auto ref = QString::fromStdString(s.value("script_ref", std::string{}));
        if (!ref.isEmpty()) {
            step.script = SetupPlanParser::resolve_script_ref(ref);
            if (step.script.isEmpty()) {
                LSW_WARN("setup_plan: unknown script_ref '{}'", ref.toStdString());
                return false;
            }
        } else {
            step.script = QString::fromStdString(s.value("script", std::string{}));
        }
        return !step.script.isEmpty();
    }
    if (kind == "download_extract") {
        step.kind = StepKind::DownloadExtract;
        step.url = QString::fromStdString(s.value("url", std::string{}));
        step.extract_to = QString::fromStdString(s.value("extract_to", std::string{}));
        const auto ref = QString::fromStdString(s.value("script_ref", std::string{}));
        if (!ref.isEmpty()) {
            step.script = SetupPlanParser::resolve_script_ref(ref);
            if (step.script.isEmpty()) {
                LSW_WARN("setup_plan: unknown script_ref '{}'", ref.toStdString());
                return false;
            }
        } else {
            step.script = QString::fromStdString(s.value("script", std::string{}));
        }
        return !step.url.isEmpty();
    }
    return false;
}

}  // namespace

QString SetupPlanParser::resolve_script_ref(const QString& ref) {
    if (ref == QStringLiteral("tune-windows")) {
        return QString::fromUtf8(lsw::embedded::kScriptTuneWindows);
    }
    return {};
}

QVector<SetupStage> SetupPlanParser::parse(const QByteArray& json, QString* error_out) {
    QVector<SetupStage> out;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json.constData(), json.constData() + json.size(),
                                    /*cb=*/nullptr,
                                    /*allow_exceptions=*/true,
                                    /*ignore_comments=*/true);
    } catch (const nlohmann::json::exception& e) {
        if (error_out != nullptr) {
            *error_out = QString::fromUtf8(e.what());
        }
        return out;
    }
    if (!doc.is_object() || !doc.contains("steps") || !doc["steps"].is_array()) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("missing 'steps' array");
        }
        return out;
    }
    for (const auto& entry : doc["steps"]) {
        SetupStage stage;
        if (entry.is_array()) {
            for (const auto& item : entry) {
                SetupStep s;
                if (parse_one_step(item, s)) {
                    stage.steps.push_back(std::move(s));
                }
            }
        } else if (entry.is_object()) {
            SetupStep s;
            if (parse_one_step(entry, s)) {
                stage.steps.push_back(std::move(s));
            }
        }
        if (!stage.steps.isEmpty()) {
            out.push_back(std::move(stage));
        }
    }
    return out;
}

}  // namespace lsw::host::provision
