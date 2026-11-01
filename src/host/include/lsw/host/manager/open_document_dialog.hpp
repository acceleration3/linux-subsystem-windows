#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

namespace lsw::host::manager {

// "Windows can't see this file": shown when files are opened from the host
// and some are in no folder shared with the guest. Names them and their
// folders and offers the two ways forward, sharing the folders (which
// takes a restart of Windows) or opening copies that stay in Windows.
// Parentless by design: the manager's window may be hidden, and the dialog
// is the whole of what the person is looking at.
class OpenDocumentDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { Cancel, Share, Copy };

    // ``files`` are the host paths Windows cannot see; ``folders`` the
    // folders sharing would add. ``application`` names what will open them
    // (empty: "the application"). ``guest_running`` says whether sharing
    // is a restart or only a start. ``unshareable`` counts files whose
    // name Windows cannot use even in a shared folder, which open as copies
    // either way.
    OpenDocumentDialog(const QStringList& files, const QStringList& folders,
                       const QString& application, bool guest_running, int unshareable,
                       QWidget* parent = nullptr);

    [[nodiscard]] Choice choice() const noexcept { return choice_; }

private:
    Choice choice_ = Choice::Cancel;
};

}  // namespace lsw::host::manager
