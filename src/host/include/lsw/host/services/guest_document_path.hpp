#pragma once

#include "lsw/host/vm/domain_description.hpp"

#include <QString>
#include <QVector>
#include <optional>

namespace lsw::host::services {

// Where a file on this machine is in Windows: the drive of the shared
// folder it sits in.
//
// A shared folder is a virtio-fs device tagged with its drive letter
// (see GuestBundle::mounts_of), so a file under one has the path ``Z:\rel\path`` in
// the guest. A file under none cannot be opened there, and neither can
// one whose name Windows cannot spell.
class GuestDocumentPath {
public:
    GuestDocumentPath() = delete;

    // The guest path of ``host_path``, an absolute path already run
    // through ``canonical``; the longest share containing it wins. nullopt
    // when no share does, or when a part of the path below the share is
    // not a name Windows allows: one of ``<>:"|?*``, a control character,
    // a trailing dot or space, or a reserved device name (CON, NUL, COM1
    // ... with or without an extension). The shares of a definition that
    // are not a single drive letter are no share of LSW's and are ignored.
    [[nodiscard]] static std::optional<QString>
    map(const QString& host_path, const QVector<vm::DomainDescription::Share>& shares);

    // ``path`` with its symlinks resolved, empty when it does not exist.
    // virtiofsd cannot follow a link out of its share, so the real path is
    // what has to be compared with the shares; a share's own folder gets
    // the same treatment.
    [[nodiscard]] static QString canonical(const QString& path);

    // Whether ``path`` is ``root`` or below it, by whole components.
    [[nodiscard]] static bool within(const QString& path, const QString& root);

    // Whether one path component is a name Windows can create.
    [[nodiscard]] static bool legal_component(const QString& name);
};

}  // namespace lsw::host::services
