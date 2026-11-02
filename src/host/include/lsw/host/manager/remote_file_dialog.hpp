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
class RemoteFileDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode : std::uint8_t {
        Program,
        AnyFile,
        Directory,
    };

    RemoteFileDialog(link::AgentRpcClient& rpc, Mode mode, QWidget* parent = nullptr);

    [[nodiscard]] QString selected_path() const noexcept { return selected_; }
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

}
