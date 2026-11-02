#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/app/app.hpp"
#include "lsw/host/app/cli_parser.hpp"

int main(int argc, char** argv) {
    ::lsw::base::Logger::init("manager");
    ::lsw::host::app::App app{argc, argv};
    app.set_cli_options(::lsw::host::app::CliParser::parse(app.arguments()));
    return app.run();
}
