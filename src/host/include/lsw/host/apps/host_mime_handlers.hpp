#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::apps {

// Whether something on this machine, other than LSW, opens a file type.
//
// A desktop entry that declares a type competes with the host's own
// applications for it, and the desktop's choice among them is not one LSW
// can steer: KDE's, measured, is different from one rebuild of its cache to
// the next, whatever preference an entry names, and an explicit default is
// the one thing that holds, which is the person's to write. So an entry of
// LSW's declares only the types nothing else handles, where it is rightly
// the handler, and for the rest the person says so with "Make it the
// default".
//
// Stateless: answered from the desktop's caches as they are, and nothing is
// written anywhere. Built once (it reads the caches) and asked many times.
class HostMimeHandlers {
public:
    // Where things are read from. The defaults are the real environment; a
    // test points them at a temporary tree.
    struct Dirs {
        QString config_home;    // $XDG_CONFIG_HOME: mimeapps.list is read, never written
        QString data_home;      // $XDG_DATA_HOME
        QStringList data_dirs;  // $XDG_DATA_DIRS, in order

        [[nodiscard]] static Dirs from_environment();
    };

    explicit HostMimeHandlers(const Dirs& dirs);

    // An installed application that is not LSW's (a desktop entry not named
    // ``lsw-*``) is listed for ``type`` or one of its aliases, in the caches
    // of the data home and of each data directory; or ``type`` is a text
    // type and text/plain has one. Not through any other parent: .xlsx is a
    // zip, and the archiver does not open spreadsheets.
    [[nodiscard]] bool has_host_handler(const QString& type) const;

    // mimeapps.list names ``entry`` as the default for ``type``: the person's
    // own choice, which is declared whatever else handles it.
    [[nodiscard]] bool explicit_default(const QString& type, const QString& entry) const;

private:
    [[nodiscard]] bool installed(const QString& id) const;

    Dirs dirs_;
    // Each cache's "type -> entries", in the order of the directories.
    QVector<QHash<QString, QStringList>> caches_;
    // The [Default Applications] of mimeapps.list.
    QHash<QString, QStringList> defaults_;
    mutable QHash<QString, bool> installed_;
};

}  // namespace lsw::host::apps
