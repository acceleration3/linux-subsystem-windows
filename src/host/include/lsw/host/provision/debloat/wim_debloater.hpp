#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

namespace lsw::host::provision::debloat {

// Takes components out of an install.wim, offline, on this machine.
//
// The image is edited in place through wimlib: paths are deleted from
// the image's directory tree and the result is written back. Nothing is
// mounted, so this needs no FUSE and no root, and nothing runs in the
// guest — the removed files are never written to the VM's disk in the
// first place.
//
// WHAT THIS IS NOT. MicroWin and tiny11 do the same job by driving
// DISM, which understands the component store: it unregisters a package
// as it removes the payload, and the servicing stack stays consistent.
// There is no DISM on Linux. Deleting a package's files and its
// registration by hand gets the same bytes off the disk and leaves the
// component store believing in something that is gone — which is
// survivable for a guest that installs once and is thrown away, and is
// why ``DebloatLevel::Maximum`` warns that it is one-way.
//
// A ``Sane`` pass stays inside what can be removed cleanly: provisioned
// application payloads, which are staged files rather than serviced
// components, and a setup stub.
class WimDebloater : public QObject {
    Q_OBJECT
public:
    explicit WimDebloater(QObject* parent = nullptr);

    // Apply ``plan`` to image ``image_index`` of the WIM at
    // ``wim_path``, rewriting the file in place.
    //
    // Deletions and registry edits happen in one pass. They could be
    // two classes calling wimlib twice, and that would rewrite several
    // gigabytes twice: the hives are pulled out first, edited on disk,
    // and put back in the same update batch as the deletions, so the
    // image is written exactly once.
    //
    // False on failure, with ``error_out`` naming the step and the
    // wimlib error. A path in the plan that is not in the image is not
    // a failure: editions differ in what they ship, and a plan is a
    // list of things to remove if present.
    [[nodiscard]] bool apply(const QString& wim_path, int image_index, const DebloatPlan& plan,
                             QString* error_out);

    // Write image ``image_index`` of ``src_path`` to ``dest_path`` as
    // the only image in a new WIM.
    //
    // This is what makes a debloat worth doing. A WIM is content
    // addressed and Windows media carries eleven editions sharing one
    // pool of resources, so deleting a file from one image frees
    // nothing while the other ten still reference it - measured at 63
    // MiB reclaimed from removing 84 application payloads. Exporting a
    // single image drops every resource the others were holding, and
    // the guest only installs one edition anyway.
    //
    // ``extra_parts`` are the parts after the first when ``src_path``
    // is part 1 of a split WIM, and empty otherwise. Part 1 carries the
    // metadata but only some of the file data, so an export that is not
    // given the rest fails on the first resource it cannot find.
    //
    // The result has one image, so callers debloat index 1 of it. It is
    // a whole WIM however the source arrived: isolating one edition is
    // what makes the file small enough to stop being split at all.
    //
    // Refuses an edition older than Windows 11 24H2 (build 26100), the
    // first the guest's display driver loads on, with ``error_out``
    // saying so: every install passes through here before anything is
    // written, so it is where unusable media is cheapest to turn away.
    [[nodiscard]] static bool isolate_edition(const QString& src_path,
                                              const QStringList& extra_parts, int image_index,
                                              const QString& dest_path, QString* error_out);

    // What the last ``apply`` removed, one entry per deleted path. For
    // the log and the wizard's summary.
    [[nodiscard]] const QStringList& removed() const noexcept { return removed_; }

Q_SIGNALS:
    // Coarse progress: the phase name, and how far through it we are.
    void progress(const QString& phase, int percent);

private:
    // Immediate children of ``root`` in the image, full paths. Empty
    // when the image has no such directory, which is not an error:
    // editions differ in what they ship.
    [[nodiscard]] static QStringList children_of(void* wim, int image_index,
                                                 const QString& root);

    // Children of ``root`` whose name starts with any of ``prefixes``.
    [[nodiscard]] static QStringList children_matching(void* wim, int image_index,
                                                       const QString& root,
                                                       const QStringList& prefixes);

    // Children of ``root`` whose name contains NONE of ``keep``. The
    // inverse of the above, for WinSxS, where what stays can be named
    // and what goes cannot.
    [[nodiscard]] static QStringList children_not_keeping(void* wim, int image_index,
                                                          const QString& root,
                                                          const QStringList& keep);

    QStringList removed_;
};

}  // namespace lsw::host::provision::debloat
