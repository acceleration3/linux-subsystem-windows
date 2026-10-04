#include "lsw/host/provision/debloat/wim_debloater.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/debloat/registry_tweaks.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <deque>
#include <vector>
#include <wimlib.h>

namespace lsw::host::provision::debloat {

namespace {

// Where Windows stages the payload of a provisioned application before
// any user has logged in.
//
// One entry, and measured rather than assumed: a Windows 11 25H2 image
// has this directory and has no /Windows/InfusedApps at all, which is
// where an earlier version of this list looked first. A root that is
// not there is skipped rather than failing, so adding one back costs
// nothing if a future build moves them again.
const QString kAppxRoot = QStringLiteral("/Program Files/WindowsApps");

// The two directories that hold most of a Windows image.
const QString kDriverStore = QStringLiteral("/Windows/System32/DriverStore/FileRepository");
const QString kWinSxS = QStringLiteral("/Windows/WinSxS");

// The offline hives, by their path in the image. NTUSER.DAT is the
// template every new profile is copied from, which is why a per-user
// setting can be written before any user exists.
const QStringList kHivePaths = {
    QStringLiteral("/Windows/System32/config/SOFTWARE"),
    QStringLiteral("/Windows/System32/config/SYSTEM"),
    QStringLiteral("/Windows/System32/config/DEFAULT"),
    QStringLiteral("/Users/Default/NTUSER.DAT"),
};

[[nodiscard]] QString wim_error(int rc) {
    const wimlib_tchar* text = wimlib_get_error_string(static_cast<wimlib_error_code>(rc));
    return text != nullptr ? QString::fromUtf8(text) : QStringLiteral("wimlib error %1").arg(rc);
}

// The oldest Windows build the guest's display driver loads on.
// avgpu.sys declares WDDM 3.2, and Windows 11 refuses a driver that
// declares an older level than its own, so there is no build of it for
// anything before 24H2 - and a guest installed from older media comes
// up with no display driver at all.
constexpr int kMinimumWindowsBuild = 26100;

// Collects the immediate children of a directory. wimlib hands each
// entry to this callback; the context is the list being filled.
struct ChildCollector {
    QStringList* out;
    QString parent;
};

int collect_child(const struct wimlib_dir_entry* entry, void* context) {
    auto* collector = static_cast<ChildCollector*>(context);
    const QString name = QString::fromUtf8(entry->filename != nullptr ? entry->filename : "");
    if (name.isEmpty()) {
        return 0;  // the directory itself, which iteration reports first
    }
    collector->out->append(collector->parent + QLatin1Char('/') + name);
    return 0;
}

}  // namespace

WimDebloater::WimDebloater(QObject* parent) : QObject(parent) {}

QStringList WimDebloater::children_of(void* wim, int image_index, const QString& root) {
    QStringList children;
    ChildCollector collector{&children, root};
    // Children only: a payload directory is the unit of removal, and
    // walking into it would produce thousands of paths that all
    // disappear with their parent anyway.
    const int rc = wimlib_iterate_dir_tree(
        static_cast<WIMStruct*>(wim), image_index, root.toUtf8().constData(),
        WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN, collect_child, &collector);
    if (rc != 0) {
        LSW_DEBUG("debloat: no {} in image ({})", root.toStdString(),
                  wim_error(rc).toStdString());
        return {};
    }
    return children;
}

QStringList WimDebloater::children_matching(void* wim, int image_index, const QString& root,
                                            const QStringList& prefixes) {
    QStringList matches;
    if (prefixes.isEmpty()) {
        return matches;
    }
    for (const QString& child : children_of(wim, image_index, root)) {
        const QString leaf = child.section(QLatin1Char('/'), -1);
        for (const QString& prefix : prefixes) {
            if (leaf.startsWith(prefix, Qt::CaseInsensitive)) {
                matches.append(child);
                break;
            }
        }
    }
    return matches;
}

QStringList WimDebloater::children_not_keeping(void* wim, int image_index, const QString& root,
                                               const QStringList& keep) {
    QStringList doomed;
    if (keep.isEmpty()) {
        return doomed;  // an empty keep-list would mean "delete everything"
    }
    for (const QString& child : children_of(wim, image_index, root)) {
        const QString leaf = child.section(QLatin1Char('/'), -1);
        bool kept = false;
        for (const QString& token : keep) {
            if (leaf.contains(token, Qt::CaseInsensitive)) {
                kept = true;
                break;
            }
        }
        if (!kept) {
            doomed.append(child);
        }
    }
    return doomed;
}

bool WimDebloater::isolate_edition(const QString& src_path, const QStringList& extra_parts,
                                   int image_index, const QString& dest_path,
                                   QString* error_out) {
    WIMStruct* src = nullptr;
    if (const int rc = wimlib_open_wim(src_path.toUtf8().constData(), 0, &src); rc != 0) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("cannot open %1: %2").arg(src_path, wim_error(rc));
        }
        return false;
    }
    // The cheapest moment to refuse media the guest cannot use: the
    // edition's metadata is open, and nothing has been written yet. A
    // build wimlib cannot read is let through rather than guessed at.
    if (const wimlib_tchar* build =
            wimlib_get_image_property(src, image_index, "WINDOWS/VERSION/BUILD");
        build != nullptr) {
        bool numeric = false;
        const int number = QString::fromUtf8(build).toInt(&numeric);
        if (numeric && number < kMinimumWindowsBuild) {
            wimlib_free(src);
            if (error_out != nullptr) {
                *error_out = QStringLiteral("this image is Windows build %1, and the guest's "
                                            "display driver (accel-virt) needs Windows 11 24H2, "
                                            "build %2, or later. Use newer Windows 11 media")
                                 .arg(number)
                                 .arg(kMinimumWindowsBuild);
            }
            return false;
        }
    } else {
        LSW_WARN("debloat: edition {} of {} names no build; not checking it against {}",
                 image_index, src_path.toStdString(), kMinimumWindowsBuild);
    }
    // A split image's metadata is all in part 1, so the open above
    // succeeds and reports the edition perfectly well — and then the
    // export below reads file data, most of which is in the parts that
    // have not been mentioned yet. Hand them over before that happens.
    if (!extra_parts.isEmpty()) {
        std::vector<QByteArray> held;
        std::vector<const char*> refs;
        held.reserve(static_cast<std::size_t>(extra_parts.size()));
        refs.reserve(static_cast<std::size_t>(extra_parts.size()));
        for (const QString& part : extra_parts) {
            held.push_back(part.toUtf8());
            refs.push_back(held.back().constData());
        }
        if (const int rc = wimlib_reference_resource_files(
                src, refs.data(), static_cast<unsigned>(refs.size()), 0, 0);
            rc != 0) {
            wimlib_free(src);
            if (error_out != nullptr) {
                *error_out = QStringLiteral("cannot read the other %1 parts of the split "
                                            "image: %2")
                                 .arg(extra_parts.size())
                                 .arg(wim_error(rc));
            }
            return false;
        }
    }
    WIMStruct* dest = nullptr;
    // LZX rather than the media's XPRESS: this file is written once on
    // a machine with cores to spare and then read by Setup, so the
    // slower, tighter codec is paid for in the right direction.
    if (const int rc = wimlib_create_new_wim(WIMLIB_COMPRESSION_TYPE_LZX, &dest); rc != 0) {
        wimlib_free(src);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("cannot create the output image: %1").arg(wim_error(rc));
        }
        return false;
    }
    if (const int rc = wimlib_export_image(src, image_index, dest, nullptr, nullptr, 0);
        rc != 0) {
        wimlib_free(dest);
        wimlib_free(src);
        if (error_out != nullptr) {
            *error_out =
                QStringLiteral("cannot export edition %1: %2").arg(image_index).arg(wim_error(rc));
        }
        return false;
    }
    const int rc = wimlib_write(dest, dest_path.toUtf8().constData(), 1, 0, 0);
    wimlib_free(dest);
    wimlib_free(src);
    if (rc != 0) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("cannot write %1: %2").arg(dest_path, wim_error(rc));
        }
        return false;
    }
    return true;
}

bool WimDebloater::apply(const QString& wim_path, int image_index, const DebloatPlan& plan,
                         QString* error_out) {
    removed_.clear();

    WIMStruct* wim = nullptr;
    Q_EMIT progress(QStringLiteral("Opening the Windows image"), 0);
    if (const int rc = wimlib_open_wim(wim_path.toUtf8().constData(), 0, &wim); rc != 0) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("cannot open %1: %2").arg(wim_path, wim_error(rc));
        }
        return false;
    }

    // The hives come out first, while the image is still whole. They are
    // edited on disk and go back in the same batch as the deletions, so
    // several gigabytes are rewritten once rather than twice.
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        wimlib_free(wim);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("cannot create a scratch directory for the hives");
        }
        return false;
    }
    Q_EMIT progress(QStringLiteral("Reading the registry hives"), 10);
    std::vector<QByteArray> hive_args;
    std::vector<const wimlib_tchar*> hive_ptrs;
    for (const QString& path : kHivePaths) {
        hive_args.push_back(path.toUtf8());
    }
    for (const QByteArray& arg : hive_args) {
        hive_ptrs.push_back(arg.constData());
    }
    if (const int rc = wimlib_extract_paths(wim, image_index, scratch.path().toUtf8().constData(),
                                            hive_ptrs.data(), hive_ptrs.size(), 0);
        rc != 0) {
        wimlib_free(wim);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("cannot read the registry hives: %1").arg(wim_error(rc));
        }
        return false;
    }

    Q_EMIT progress(QStringLiteral("Applying registry policy"), 20);
    const QString config_dir = QDir(scratch.path()).filePath("Windows/System32/config");
    const QString ntuser = QDir(scratch.path()).filePath("Users/Default/NTUSER.DAT");
    if (!RegistryTweaks::apply(config_dir, ntuser, RegistryTweaks::for_level(plan.level()),
                               error_out)) {
        wimlib_free(wim);
        return false;
    }

    Q_EMIT progress(QStringLiteral("Finding bundled applications"), 30);
    QStringList targets = children_matching(wim, image_index, kAppxRoot, plan.appx_prefixes());
    LSW_INFO("debloat: {} provisioned application payloads matched", targets.size());
    targets += plan.paths();

    // The two that carry the gigabytes, and only at Maximum.
    if (!plan.driver_remove().isEmpty()) {
        const QStringList drivers =
            children_matching(wim, image_index, kDriverStore, plan.driver_remove());
        LSW_INFO("debloat: {} driver packages matched", drivers.size());
        targets += drivers;
    }
    if (!plan.winsxs_keep().isEmpty()) {
        const QStringList components =
            children_not_keeping(wim, image_index, kWinSxS, plan.winsxs_keep());
        LSW_INFO("debloat: {} component-store directories matched", components.size());
        targets += components;
    }

    // One update call for everything: wimlib applies the batch and
    // rewrites the metadata once, rather than once per path.
    //
    // A deque and not a vector: the commands hold raw pointers into
    // these buffers and more are appended as the batch is built, so the
    // container must be one that never moves what it already holds.
    std::deque<QByteArray> owned;
    std::vector<wimlib_update_command> commands;
    for (const QString& path : targets) {
        owned.push_back(path.toUtf8());
        wimlib_update_command cmd{};
        cmd.op = WIMLIB_UPDATE_OP_DELETE;
        cmd.delete_.wim_path = owned.back().data();
        // FORCE so a path this edition does not ship is skipped rather
        // than failing the batch; RECURSIVE because the directories are
        // the point.
        cmd.delete_.delete_flags = WIMLIB_DELETE_FLAG_FORCE | WIMLIB_DELETE_FLAG_RECURSIVE;
        commands.push_back(cmd);
    }
    // Each edited hive replaces the one in the image. An add over an
    // existing path needs the delete to have happened first, which it
    // has: wimlib applies the commands in order.
    for (const QString& path : kHivePaths) {
        const QString on_disk = QDir(scratch.path()).filePath(path.mid(1));
        if (!QFile::exists(on_disk)) {
            continue;  // an edition without this hive
        }
        owned.push_back(path.toUtf8());
        wimlib_update_command del{};
        del.op = WIMLIB_UPDATE_OP_DELETE;
        del.delete_.wim_path = owned.back().data();
        del.delete_.delete_flags = WIMLIB_DELETE_FLAG_FORCE;
        commands.push_back(del);

        owned.push_back(on_disk.toUtf8());
        QByteArray* source = &owned.back();
        owned.push_back(path.toUtf8());
        wimlib_update_command add{};
        add.op = WIMLIB_UPDATE_OP_ADD;
        add.add.fs_source_path = source->data();
        add.add.wim_target_path = owned.back().data();
        commands.push_back(add);
    }

    Q_EMIT progress(QStringLiteral("Removing components"), 40);
    if (const int rc = wimlib_update_image(wim, image_index, commands.data(), commands.size(), 0);
        rc != 0) {
        wimlib_free(wim);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("removing components failed: %1").arg(wim_error(rc));
        }
        return false;
    }

    // Writing back is the expensive half: wimlib rebuilds the file
    // without the deleted resources. Solid compression is deliberately
    // not requested — it would shrink the result further at a cost in
    // time that dwarfs the install it is meant to speed up.
    Q_EMIT progress(QStringLiteral("Writing the trimmed image"), 55);
    if (const int rc = wimlib_overwrite(wim, WIMLIB_WRITE_FLAG_REBUILD, 0); rc != 0) {
        wimlib_free(wim);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("writing %1 failed: %2").arg(wim_path, wim_error(rc));
        }
        return false;
    }
    wimlib_free(wim);

    removed_ = targets;
    Q_EMIT progress(QStringLiteral("Image trimmed"), 100);
    LSW_INFO("debloat: removed {} paths from image {} of {}", removed_.size(), image_index,
             wim_path.toStdString());
    return true;
}

}  // namespace lsw::host::provision::debloat
