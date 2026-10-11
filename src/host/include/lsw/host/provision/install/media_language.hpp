#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {

// The languages Windows Setup can run in on a Windows medium, one of which
// the answer file has to name.
//
// Setup takes the language for its own pages, and the locales, from the
// answer file, and an answer naming a language the medium does not carry
// is one it cannot apply: it stops on its language page and waits. A
// British medium carries en-GB and no en-US. The medium says which it
// carries in sources/lang.ini, under [Available UI Languages], the
// default first; most media carry one.
class MediaLanguage {
public:
    // What lang.ini's text names under [Available UI Languages], in its
    // order (the default first), e.g. {"en-GB"}.
    [[nodiscard]] static QStringList parse(const QString& lang_ini);

    // The languages of the medium at ``iso_path``, read through 7-Zip into
    // a scratch directory under ``scratch_dir``; empty, with ``error_out``
    // set, when they cannot be read.
    [[nodiscard]] static QStringList of(const QString& iso_path, const QString& scratch_dir,
                                        QString* error_out);
};

}  // namespace lsw::host::provision::install
