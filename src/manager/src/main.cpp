// lsw-manager entry point.
//
// Just enough to construct the App composition root and run it. Every
// long-lived service is owned by ``App`` (see app.hpp); ``main``'s only
// job is to spin up the QApplication-derived class and forward exit
// codes back to the shell.

#include "lsw/log.hpp"
#include "lsw/manager/app.hpp"
#include "lsw/manager/cli.hpp"

int main(int argc, char** argv) {
    ::lsw::log::init("manager");
    ::lsw::manager::App app{argc, argv};
    // parse_cli reads the QApplication arguments after the QApplication
    // (and its event-name munging) has been constructed.
    app.set_cli_options(::lsw::manager::parse_cli(app.arguments()));
    return app.run();
}
