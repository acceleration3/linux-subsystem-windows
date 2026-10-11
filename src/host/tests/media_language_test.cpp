// The languages a Windows medium carries, from its sources/lang.ini: what
// the answer file may name, the default first.

#include "lsw/host/provision/install/media_language.hpp"

#include <cstdio>

using lsw::host::provision::install::MediaLanguage;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}  // namespace

int main() {
    // A British medium, as Microsoft ships it.
    check(MediaLanguage::parse(QStringLiteral("[Available UI Languages]\r\n"
                                              "en-GB = 3\r\n"
                                              "\r\n"
                                              "[Fallback Languages]\r\n"
                                              "en-GB = en-us\r\n")) ==
              QStringList{QStringLiteral("en-GB")},
          "a British medium carries en-GB, and the fallback section is not a language it has");
    check(MediaLanguage::parse(QStringLiteral("[Available UI Languages]\n"
                                              "de-DE = 3\n"
                                              "fr-FR = 2\n"
                                              "sr-Latn-RS = 2\n"
                                              "de-de = 1\n")) ==
              QStringList({QStringLiteral("de-DE"), QStringLiteral("fr-FR"),
                           QStringLiteral("sr-Latn-RS")}),
          "several, in the medium's order with its default first, once each");
    check(MediaLanguage::parse(QStringLiteral("[available ui languages]\nen-US=3\n")) ==
              QStringList{QStringLiteral("en-US")},
          "the section name's case and the spacing around = do not matter");
    // Microsoft's own en-US medium (Win11_25H2_English_x64), byte for byte.
    check(MediaLanguage::parse(QStringLiteral("\r\n[Available UI Languages]\r\nen-us = 3\r\n\r\n"
                                              "[Fallback Languages]\r\nen-us = en-us\r\n")) ==
              QStringList{QStringLiteral("en-US")},
          "a tag in lower case, as the media write it, in its usual case");
    check(MediaLanguage::parse(QStringLiteral("[Available UI Languages]\nsr-latn-rs = 3\n")) ==
              QStringList{QStringLiteral("sr-Latn-RS")},
          "a script subtag in title case");
    check(MediaLanguage::parse(QStringLiteral("en-US = 3\n[Other]\nfr-FR = 1\n")).isEmpty(),
          "nothing outside the section counts");
    check(MediaLanguage::parse(QString{}).isEmpty(), "an empty file names none");
    return g_failures == 0 ? 0 : 1;
}
