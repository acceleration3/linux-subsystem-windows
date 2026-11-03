#include "lsw/host/services/app_errors.hpp"

#include "lsw/base/log.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QMetaObject>

#include <nlohmann/json.hpp>

namespace lsw::host::services {

namespace {

using Problem = AppErrorReport::Problem;

QString text(const nlohmann::json& f, const char* key) {
    const auto it = f.find(key);
    return it != f.end() && it->is_string() ? QString::fromStdString(it->get<std::string>())
                                            : QString{};
}

quint32 number(const nlohmann::json& f, const char* key) {
    const auto it = f.find(key);
    return it != f.end() && it->is_number_unsigned() ? it->get<quint32>() : 0;
}

Problem::Kind problem_kind(const QString& name) {
    if (name == QLatin1String("wrong_bitness")) {
        return Problem::Kind::WrongBitness;
    }
    if (name == QLatin1String("missing_export")) {
        return Problem::Kind::MissingExport;
    }
    if (name == QLatin1String("invalid")) {
        return Problem::Kind::Invalid;
    }
    return Problem::Kind::Missing;
}

QString file_name(const QString& path) {
    const qsizetype slash = qMax(path.lastIndexOf(QLatin1Char('\\')), path.lastIndexOf(QLatin1Char('/')));
    return slash < 0 ? path : path.mid(slash + 1);
}

AppErrorReport parse(const nlohmann::json& f) {
    AppErrorReport report;
    report.kind = text(f, "kind") == QLatin1String("startup") ? AppErrorReport::Kind::Startup
                                                               : AppErrorReport::Kind::Crash;
    report.image = text(f, "image");
    report.pid = number(f, "pid");
    report.parent_pid = number(f, "parent_pid");
    report.app_id = text(f, "app_id");
    report.exit_code = number(f, "exit_code");
    report.exit_code_hex = text(f, "exit_code_hex");
    report.code_name = text(f, "code_name");
    report.note = text(f, "note");
    report.module = text(f, "module");
    report.offset = text(f, "offset");
    report.module_version = text(f, "module_version");
    report.app_version = text(f, "app_version");
    if (const auto it = f.find("problems"); it != f.end() && it->is_array()) {
        for (const auto& item : *it) {
            Problem problem;
            problem.kind = problem_kind(text(item, "kind"));
            problem.module = text(item, "module");
            problem.importer = text(item, "importer");
            problem.function = text(item, "function");
            problem.module_bits = static_cast<int>(number(item, "module_bits"));
            problem.image_bits = static_cast<int>(number(item, "image_bits"));
            if (!report.problems.contains(problem)) {
                report.problems.append(problem);
            }
        }
    }
    if (const auto it = f.find("event_text"); it != f.end() && it->is_array()) {
        for (const auto& line : *it) {
            const QString value = line.is_string() ? QString::fromStdString(line.get<std::string>())
                                                   : QString{};
            if (!value.isEmpty() && !report.event_text.contains(value)) {
                report.event_text << value;
            }
        }
    }
    return report;
}

}

QString AppErrorReport::image_name() const { return file_name(image); }

QString AppErrorReport::identity() const {
    QStringList parts{image, QString::number(exit_code), note, event_text.join(QLatin1Char('\n')),
                      module, offset};
    for (const Problem& problem : problems) {
        parts << QStringLiteral("%1/%2/%3/%4")
                     .arg(static_cast<int>(problem.kind))
                     .arg(problem.module.toLower(), problem.importer.toLower(), problem.function);
    }
    return parts.join(QLatin1Char('|'));
}

QString AppErrorReport::log_line() const {
    QStringList missing;
    QStringList other;
    for (const Problem& problem : problems) {
        switch (problem.kind) {
        case Problem::Kind::Missing:
            if (!missing.contains(problem.module)) {
                missing << problem.module;
            }
            break;
        case Problem::Kind::WrongBitness:
            other << QStringLiteral("%1 is %2-bit but %3 is %4-bit")
                         .arg(problem.module).arg(problem.module_bits).arg(problem.importer)
                         .arg(problem.image_bits);
            break;
        case Problem::Kind::MissingExport:
            other << QStringLiteral("%1 missing from %2").arg(problem.function, problem.module);
            break;
        case Problem::Kind::Invalid:
            other << QStringLiteral("%1 is not a valid image").arg(problem.module);
            break;
        }
    }
    QStringList detail;
    if (!missing.isEmpty()) {
        detail << QStringLiteral("missing ") + missing.join(QStringLiteral(", "));
    }
    detail << other;
    if (!note.isEmpty()) {
        detail << note;
    }
    detail << event_text;
    QString line = QStringLiteral("app_error: %1 pid %2 %3 (%4)")
                       .arg(image_name()).arg(pid).arg(exit_code_hex, code_name);
    if (!detail.isEmpty()) {
        line += QStringLiteral(": ") + detail.join(QStringLiteral("; "));
    }
    if (!module.isEmpty()) {
        line += QStringLiteral(" in ") + module;
        if (!offset.isEmpty()) {
            line += QStringLiteral(" at ") + offset;
        }
    }
    return line;
}

AppErrors::AppErrors(link::AgentRpcClient& rpc, QObject* parent) : QObject(parent), rpc_(rpc) {
    sub_ = rpc_.subscribe(linkproto::topic::kAppError, [this](const nlohmann::json& f) {
        const AppErrorReport report = parse(f);
        LSW_WARN("{}", report.log_line().toStdString());
        QMetaObject::invokeMethod(
            this, [this, report] { Q_EMIT reported(report); }, Qt::QueuedConnection);
    });
}

AppErrors::~AppErrors() { sub_.reset(); }

}
