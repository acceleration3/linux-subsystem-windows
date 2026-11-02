#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/app/app.hpp"
#include "lsw/host/app/cli_parser.hpp"

#include <QCoreApplication>

int main(int argc, char** argv) {
    ::lsw::host::app::CliOptions options;
    {
        QCoreApplication parsing{argc, argv};
        options = ::lsw::host::app::CliParser::parse(parsing.arguments());
    }
    ::lsw::base::Logger::init("manager");
    ::lsw::host::app::App app{argc, argv};
    app.set_cli_options(std::move(options));
    return app.run();
}
