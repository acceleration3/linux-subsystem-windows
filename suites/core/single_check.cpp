#include "core/single_check.hpp"

#include "core/child_protocol.hpp"
#include "core/context.hpp"

#include <windows.h>

#include <cstdio>
#include <exception>
#include <filesystem>

namespace lsw::suites {

SingleCheck::SingleCheck(const Registry& registry, std::filesystem::path artifacts)
    : registry_(registry), artifacts_(std::move(artifacts)) {}

int SingleCheck::run(const std::string& id) const {
    ::SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    Context context(id, artifacts_);
    const Check* check = registry_.find_check(id);
    if (check == nullptr) {
        const std::string text = ChildProtocol::encode(context, Outcome::fail("no such check"));
        std::fputs(text.c_str(), stdout);
        return 2;
    }
    Outcome outcome = Outcome::fail("the check returned nothing");
    try {
        outcome = check->run(context);
    } catch (const std::exception& error) {
        outcome = Outcome::fail(std::string("exception: ") + error.what());
    } catch (...) {
        outcome = Outcome::fail("unknown exception");
    }
    if (outcome.status() != Status::Ok && !context.log_path().empty()) {
        context.artifact(context.log_path());
    }
    if (outcome.status() == Status::Skip && outcome.reason().empty()) {
        outcome = Outcome::fail("skipped without a reason");
    }
    const std::string text = ChildProtocol::encode(context, outcome);
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);
    return outcome.status() == Status::Fail ? 1 : 0;
}

}  // namespace lsw::suites
