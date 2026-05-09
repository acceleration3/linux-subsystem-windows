#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

namespace lsw::manager::vm { class AgentRpcClient; }

namespace lsw::manager::apps {

// One installed Windows app the guest agent told us about.
struct AppEntry {
    QString id;        // stable identifier (e.g. AUMID or full exe path)
    QString name;      // display name
    QString exe_path;  // absolute path inside the guest
    QImage icon;       // PNG decoded from the agent reply
};

// Asks the guest agent for the Start menu app list. Async; emits
// ``loaded`` when done. Caches the last reply per-VM.
class AppCatalog : public QObject {
    Q_OBJECT
public:
    explicit AppCatalog(vm::AgentRpcClient& rpc, QObject* parent = nullptr);

    void refresh();

    [[nodiscard]] const QList<AppEntry>& entries() const noexcept { return entries_; }

Q_SIGNALS:
    void loaded(const QList<AppEntry>& entries);
    void failed(const QString& error_message);

private:
    vm::AgentRpcClient& rpc_;
    QList<AppEntry> entries_;
};

}  // namespace lsw::manager::apps
