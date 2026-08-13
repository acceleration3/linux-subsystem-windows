// lsw-manager entry point.
//
// Just enough to construct the App composition root and run it. Every
// long-lived service is owned by ``App`` (see app.hpp); ``main``'s only
// job is to spin up the QApplication-derived class and forward exit
// codes back to the shell.

#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/app/app.hpp"
#include "lsw/host/app/cli_parser.hpp"

int main(int argc, char** argv) {
    ::lsw::base::Logger::init("manager");
    ::lsw::host::app::App app{argc, argv};
    // parse_cli reads the QApplication arguments after the QApplication
    // (and its event-name munging) has been constructed.
    app.set_cli_options(::lsw::host::app::CliParser::parse(app.arguments()));
    return app.run();
}
