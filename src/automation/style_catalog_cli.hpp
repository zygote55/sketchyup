#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

class QCommandLineParser;

namespace sketchy {

// The style and catalog flags of sketchyup-cli (ADR 0174 section 7, R100.e). Five operations, each
// one bounded workflow: every check runs before the first change, an output is always a new file
// (an existing path or a link is refused and never replaced), and the --input model is never
// changed. The options each operation takes are listed once, here, and --capabilities,
// --describe-command and the flag checks all read this list.

struct StyleCatalogOption {
    QString name;     // The flag without its dashes.
    QString argument; // The value's placeholder; empty for a switch.
    QString summary;
    bool repeatable{}; // Given once for each value.
};

// The five operation flags, in the order they are listed.
QStringList styleCatalogOperations();
// The options the flags take, beside the shared --input and --output (which main registers with
// their other operations). An operation's own flag is among them, so each one is parsed once.
const std::vector<StyleCatalogOption> &styleCatalogOptions();
// The operation named on the command line, or empty. Two operations together are refused.
QString styleCatalogOperationOf(const QCommandLineParser &parser);
// Runs the operation. Its checks come first; each refusal throws InspectionError before anything is
// published, and the JSON failure names the code and the reason.
QJsonObject runStyleCatalogCli(const QCommandLineParser &parser, const QString &operation);
// `--capabilities`: every operation with the options it takes, and every option's shape.
QJsonObject styleCatalogCliCapabilities();
// `--describe-command NAME`, for an operation: its usage, options and rules. Empty when NAME is not
// one of the five operations.
QJsonObject styleCatalogCliDescription(const QString &operation);

} // namespace sketchy
