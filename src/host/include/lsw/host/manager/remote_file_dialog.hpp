#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <cstdint>

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QVBoxLayout;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class QHBoxLayout;

namespace lsw::host::manager {

// A file picker that reads the guest.
//
// The path in an application's configuration is a Windows path, so
// browsing for it has to happen inside Windows. This asks the agent
// over the connection the manager already has, rather than going
// through the SMB share: the share exists for the user's file manager
// and can be switched off, and it would see the guest as a different
// account than the one that will run the program.
//
// It opens in the Start menu's Programs folder, which is exactly what
// the application catalog already enumerates, so picking an application
// is two clicks rather than typing a path from memory. Choosing a
// shortcut stores what it points at.
class RemoteFileDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode : std::uint8_t {
        Program,    // executables and shortcuts
        AnyFile,
        Directory,
    };

    RemoteFileDialog(link::AgentRpcClient& rpc, Mode mode, QWidget* parent = nullptr);

    // The chosen path, with a shortcut already resolved to its target.
    [[nodiscard]] QString selected_path() const noexcept { return selected_; }
    // A shortcut's own arguments and working folder, when the choice
    // was a .lnk and it carried them.
    [[nodiscard]] QString selected_arguments() const noexcept { return arguments_; }
    [[nodiscard]] QString selected_workdir() const noexcept { return workdir_; }

    void navigate_to(const QString& path);

private:
    void build();
    void append_place_widget(QWidget* widget);
    void add_place(const QString& label, const QString& path, const QString& glyph);
    void highlight_place(const QString& path);
    void add_place_heading(const QString& text);
    void refresh_places();
    void render_entries(const link::AgentReply& reply);
    void set_breadcrumb(const QString& path);
    void choose();
    void accept_current();
    [[nodiscard]] QString parent_of(const QString& path) const;
    [[nodiscard]] QString suffix_filter() const;

    link::AgentRpcClient& rpc_;
    Mode mode_ = Mode::Program;

    QString current_;
    QString selected_;
    QString arguments_;
    QString workdir_;

    QWidget* places_ = nullptr;
    QVBoxLayout* places_layout_ = nullptr;
    QVector<QPushButton*> place_buttons_;
    bool drives_listed_ = false;
    QHBoxLayout* crumbs_ = nullptr;
    QTreeWidget* entries_ = nullptr;
    QLineEdit* name_edit_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* filter_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* choose_button_ = nullptr;
};

}  // namespace lsw::host::manager
