#include "lsw/host/platform/indexer_exclusions.hpp"

#include <QMap>
#include <cstdio>
#include <vector>

using lsw::host::platform::CommandRunner;
using lsw::host::platform::IndexerExclusions;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

class FakeRunner : public CommandRunner {
public:
    bool available(const QString& program) const override { return tools.contains(program); }

    Output run(const QString& program, const QStringList& arguments) override {
        calls.push_back(program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
        if (program == QLatin1String("kreadconfig6")) {
            return {true, baloo + QLatin1Char('\n')};
        }
        if (program == QLatin1String("kwriteconfig6")) {
            baloo = arguments.last();
            return {true, {}};
        }
        if (program == QLatin1String("gsettings") && arguments.value(0) == QLatin1String("get")) {
            return {schema, tracker + QLatin1Char('\n')};
        }
        if (program == QLatin1String("gsettings")) {
            tracker = arguments.last();
            return {true, {}};
        }
        return {};
    }

    int writes() const {
        int n = 0;
        for (const QString& call : calls) {
            n += call.startsWith(QLatin1String("kwriteconfig6")) ||
                         call.startsWith(QLatin1String("gsettings set"))
                     ? 1
                     : 0;
        }
        return n;
    }

    QStringList tools{QStringLiteral("kreadconfig6"), QStringLiteral("kwriteconfig6"),
                      QStringLiteral("gsettings")};
    QString baloo;
    QString tracker = QStringLiteral("@as []");
    bool schema = true;
    std::vector<QString> calls;
};

}  // namespace

int main() {
    const QString home = QStringLiteral("/home/u");
    const QString point = QStringLiteral("/home/u/Windows");

    check(IndexerExclusions::parse_baloo(QStringLiteral("$HOME/a, /b/ ,")) ==
              QStringList({"$HOME/a", "/b/"}),
          "a Baloo list splits on commas");
    check(IndexerExclusions::parse_tracker(QStringLiteral("['po', '$HOME/it\\'s', 'x\\\\y']")) ==
              QStringList({"po", "$HOME/it's", "x\\y"}),
          "a GVariant string list is read with its escapes");
    check(IndexerExclusions::parse_tracker(QStringLiteral("@as []")).isEmpty(),
          "an empty typed list");
    check(IndexerExclusions::format_tracker({"a", "it's"}) == QStringLiteral("['a', 'it\\'s']"),
          "and written back quoted");

    bool changed = false;
    check(IndexerExclusions::merge({"$HOME/Windows/"}, point, home, changed) ==
                  QStringList({"$HOME/Windows/"}) &&
              !changed,
          "an entry that already covers it, spelled with $HOME and a slash, is kept as is");
    check(IndexerExclusions::merge({"/x"}, point, home, changed) == QStringList({"/x", point}) &&
              changed,
          "otherwise it is appended");

    {
        FakeRunner runner;
        IndexerExclusions indexers{runner, home};
        indexers.apply(point);
        check(runner.baloo == point && runner.tracker == QStringLiteral("['/home/u/Windows']"),
              "empty settings get the folder");
        const int first = runner.writes();
        indexers.apply(point);
        check(runner.writes() == first, "applying twice writes nothing the second time");
    }
    {
        FakeRunner runner;
        runner.baloo = QStringLiteral("$HOME/.cache,/mnt/data/");
        runner.tracker = QStringLiteral("['po', 'CVS', '$HOME/.local']");
        IndexerExclusions indexers{runner, home};
        indexers.apply(point);
        check(runner.baloo == QStringLiteral("/home/u/.cache,/mnt/data/,/home/u/Windows"),
              "Baloo's existing folders are kept, $HOME expanded so no flag is needed");
        check(runner.tracker == QStringLiteral("['po', 'CVS', '$HOME/.local', '/home/u/Windows']"),
              "Tracker's existing entries are kept untouched");
    }
    {
        FakeRunner runner;
        runner.tools.clear();
        IndexerExclusions indexers{runner, home};
        indexers.apply(point);
        check(runner.calls.empty(), "without the tools nothing runs");
    }
    {
        FakeRunner runner;
        runner.schema = false;
        IndexerExclusions indexers{runner, home};
        indexers.apply(point);
        check(runner.tracker == QStringLiteral("@as []") && runner.baloo == point,
              "a missing Tracker schema is skipped and Baloo still gets it");
    }
    return g_failures == 0 ? 0 : 1;
}
