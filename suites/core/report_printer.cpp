#include "core/report_printer.hpp"

#include <sstream>

namespace lsw::suites {

void ReportPrinter::print(const CheckResult& result) const {
    std::ostringstream out;
    const bool failed = result.status == Status::Fail || result.status == Status::Timeout ||
                        result.status == Status::Crash;
    if (result.status == Status::Ok) {
        out << "ok " << result.id;
    } else if (result.status == Status::Skip) {
        out << "skip " << result.id;
    } else {
        out << "FAIL " << result.id << " status=" << Outcome::name(result.status);
    }
    out << ' ' << result.ms << "ms\n";
    if (!result.reason.empty()) {
        out << "note " << result.id << ' ' << result.reason << '\n';
    }
    for (const std::string& note : result.notes) {
        out << "note " << result.id << ' ' << note << '\n';
    }
    if (failed && !result.output.empty()) {
        std::istringstream lines(result.output);
        std::string line;
        int shown = 0;
        while (shown < 20 && std::getline(lines, line)) {
            out << "    | " << line << '\n';
            ++shown;
        }
    }
    std::fputs(out.str().c_str(), stdout);
    std::fflush(stdout);
}

void ReportPrinter::summary(int passed, int failed, int skipped) const {
    std::printf("%s: %d passed, %d failed, %d skipped\n", exe_.c_str(), passed, failed, skipped);
    std::fflush(stdout);
}

}  // namespace lsw::suites
