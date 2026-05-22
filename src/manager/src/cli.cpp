#include "lsw/manager/cli.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

namespace lsw::manager {

CliOptions parse_cli(const QStringList& argv) {
    QCommandLineParser p;
    p.setApplicationDescription(
        "LSW manager. With no flags, launches the GUI wizard (first run) or the "
        "manager window (subsequent runs). With install flags supplied, runs the "
        "install pipeline headlessly and prints step transitions / progress to "
        "stdout, exiting with code 0 on success and non-zero on failure.");
    p.addHelpOption();

    QCommandLineOption accept_license({"accept-license", "L"},
        "Pre-check the Windows license acceptance box.");
    QCommandLineOption iso_opt({"iso", "i"},
        "Path to a local Windows ISO. Mutually exclusive with --edition.", "path");
    QCommandLineOption edition_opt({"edition", "e"},
        "Microsoft download edition (substring of the edition label, e.g. \"Windows 11\").",
        "label");
    QCommandLineOption language_opt({"language", "l"},
        "Language for the Microsoft download (substring, e.g. \"English\").", "label");
    QCommandLineOption user_opt({"user", "u"},
        "Local Windows account username to create.", "name");
    QCommandLineOption pass_opt({"password", "p"},
        "Local Windows account password.", "pass");
    QCommandLineOption computer_opt({"computer-name", "N"},
        "Windows computer (host) name. Defaults to the LSW VM name.", "name");
    QCommandLineOption cpu_opt({"cpu", "c"},
        "VM CPU core count.", "n");
    QCommandLineOption mem_opt({"memory", "m"},
        "VM memory in MiB.", "mib");
    QCommandLineOption disk_opt({"disk", "d"},
        "VM disk size in GiB.", "gib");
    QCommandLineOption launch_opt("launch",
        "Launch the named guest app and exit (forwards to a running "
        "manager via D-Bus; starts one if none is up).", "app-id");

    p.addOption(accept_license);
    p.addOption(iso_opt);
    p.addOption(edition_opt);
    p.addOption(language_opt);
    p.addOption(user_opt);
    p.addOption(pass_opt);
    p.addOption(computer_opt);
    p.addOption(cpu_opt);
    p.addOption(mem_opt);
    p.addOption(disk_opt);
    p.addOption(launch_opt);

    p.process(argv);

    CliOptions o;
    o.accept_license = p.isSet(accept_license);
    o.iso_path = p.value(iso_opt);
    o.edition_label = p.value(edition_opt);
    o.language_label = p.value(language_opt);
    o.user_name = p.value(user_opt);
    o.user_password = p.value(pass_opt);
    o.computer_name = p.value(computer_opt);
    if (p.isSet(cpu_opt)) {
        o.cpu_cores = p.value(cpu_opt).toInt();
    }
    if (p.isSet(mem_opt)) {
        o.memory_mib = p.value(mem_opt).toInt();
    }
    if (p.isSet(disk_opt)) {
        o.disk_gib = p.value(disk_opt).toInt();
    }
    o.launch_app_id = p.value(launch_opt);
    return o;
}

}  // namespace lsw::manager
