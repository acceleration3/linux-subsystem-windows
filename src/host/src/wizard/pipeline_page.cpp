#include "lsw/host/wizard/pipeline_page.hpp"

#include <QTimer>
#include <QWizard>

namespace lsw::host::wizard {

void PipelinePage::reset_complete() {
    complete_ = false;
    Q_EMIT completeChanged();
}

void PipelinePage::finish_and_advance() {
    complete_ = true;
    Q_EMIT completeChanged();
    QTimer::singleShot(0, this, [this] {
        if (auto* w = wizard()) {
            w->next();
        }
    });
}

}  // namespace lsw::host::wizard
