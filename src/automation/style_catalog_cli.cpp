#include "automation/style_catalog_cli.hpp"
#include "automation/catalog_commands.hpp"
#include "automation/inspection.hpp"
#include "automation/style_commands.hpp"
#include "core/model.hpp"
#include "io/catalog_insert.hpp"
#include "io/catalog_io.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/style_bundle.hpp"
#include <QByteArray>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <stdexcept>

namespace sketchy {
namespace {

struct OperationSpec {
    QString name, usage, summary;
    QStringList required; // Options that must be set, besides the operation's own flag.
    QStringList options;  // Options it takes, besides its own flag.
    QStringList rules;
};

const std::vector<OperationSpec> &operationSpecs() {
    static const std::vector<OperationSpec> specs{
        {"import-style",
         "sketchyup-cli --input MODEL --import-style FILE.sketchystyle --output NEW.sketchyup",
         "Apply a .sketchystyle file to --input and write the result to a new native file. The "
         "model is not changed.",
         {"input", "output"},
         {"input", "output"},
         {"A missing or hash-mismatched edge texture refuses the import and names the texture; "
          "nothing is written and no texture is substituted.",
          "A style field that fails validation refuses the import.",
          "--output must be a new file: an existing file or a link is refused."}},
        {"export-style",
         "sketchyup-cli --input MODEL --export-style NEW.sketchystyle [--name NAME] "
         "[--description TEXT] [--label TEXT]...",
         "Write the document style of --input, with its edge texture, to a new .sketchystyle file. "
         "The model is not changed.",
         {"input"},
         {"input", "name", "description", "label"},
         {"The file must end in .sketchystyle and must not exist.",
          "--name defaults to the file's base name.",
          "A document whose edge texture has no bytes is refused; nothing is written."}},
        {"list-catalog",
         "sketchyup-cli --list-catalog FILE.sketchycat | NAME",
         "Print the catalog.list result of a catalog: its entries, kinds, importable state and "
         "reasons. Standalone; it reads no model.",
         {},
         {},
         {"A value with a slash, or ending in .sketchycat, is a file. Any other value names a "
          "catalog in the catalog library folder.",
          "Output is the same JSON as the catalog.list query."}},
        {"import-catalog",
         "sketchyup-cli --input MODEL --import-catalog FILE.sketchycat | NAME --entries NAME "
         "[--entries NAME]... --output NEW.sketchyup",
         "Import the named entries of a catalog into --input and write the result to a new native "
         "file. The model is not changed.",
         {"input", "output", "entries"},
         {"input", "output", "entries"},
         {"Each --entries NAME must match exactly one entry of the catalog.",
          "A refused entry refuses the whole import. The refusal names the entry and the missing "
          "dependency, and nothing is written.",
          "A catalog file is named as for --list-catalog.", "--output must be a new file."}},
        {"export-catalog",
         "sketchyup-cli --input MODEL --export-catalog NEW.sketchycat --catalog-component ID "
         "[--catalog-material ID]... [--catalog-style] [--name NAME] ...\n"
         "sketchyup-cli --input MODEL --export-catalog NAME --to-library ...",
         "Capture the chosen components, materials and the document style of --input into a new "
         ".sketchycat file, or into the catalog library folder with --to-library. The model is not "
         "changed.",
         {"input"},
         {"input", "name", "description", "label", "catalog-version", "catalog-license",
          "catalog-component", "catalog-material", "catalog-style", "to-library"},
         {"Without --to-library the value is a path that ends in .sketchycat and must not exist.",
          "With --to-library the value is a name; the file NAME.sketchycat is written into the "
          "catalog library folder and must not exist.",
          "Choose at least one of --catalog-component, --catalog-material and --catalog-style.",
          "--catalog-version defaults to 1.0.0 and --catalog-license to LicenseRef-Unspecified."}},
    };
    return specs;
}

const OperationSpec *findOperation(const QString &name) {
    for (const auto &spec : operationSpecs())
        if (spec.name == name)
            return &spec;
    return nullptr;
}

[[noreturn]] void refuse(const char *code, const QString &message,
                         const QJsonObject &details = {}) {
    throw InspectionError(code, message.toStdString(), details);
}

// Turns a refusal from the layer below into the CLI's code. A refusal the CLI made itself passes.
template <typename Work> auto asCode(const char *code, Work &&work) -> decltype(work()) {
    try {
        return work();
    } catch (const InspectionError &) {
        throw;
    } catch (const std::runtime_error &error) {
        refuse(code, QString::fromUtf8(error.what()));
    }
}

// The file read as a bad input: an unreadable, malformed or refused file is INPUT_ERROR.
template <typename Work> auto readingFile(Work &&work) -> decltype(work()) {
    return asCode("INPUT_ERROR", std::forward<Work>(work));
}

// An output is a new file. An empty path, an existing file and a link are all refused, and the
// check runs before any work, so a refusal leaves nothing behind.
void requireNewFile(const QString &path, const QString &flag) {
    const QFileInfo info(path);
    if (path.isEmpty() || info.exists() || info.isSymLink())
        refuse("INVALID_REQUEST",
               "--" + flag + " needs a new file; existing files are never replaced");
}

void requireSuffix(const QString &path, const QString &suffix, const QString &flag) {
    if (!path.endsWith(suffix, Qt::CaseInsensitive))
        refuse("INVALID_REQUEST", "--" + flag + " writes a file whose name ends in " + suffix);
}

// The base name of a path without its format suffix: the default name of a style or catalog.
QString baseName(const QString &path, const QString &suffix) {
    auto name = QFileInfo(path).fileName();
    if (name.endsWith(suffix, Qt::CaseInsensitive))
        name.chop(suffix.size());
    return name;
}

// The bytes of one input file, bounded before they are read.
QByteArray readInputFile(const QString &path, qsizetype limit, const char *what) {
    const QFileInfo info(path);
    QFile file(path);
    if (!info.isFile() || info.size() > limit || !file.open(QIODevice::ReadOnly))
        refuse("INPUT_ERROR", QString::fromLatin1(what) + " must be a readable file of at most " +
                                  QString::number(limit) + " bytes");
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > limit)
        refuse("INPUT_ERROR", QString::fromLatin1(what) + " could not be read in full");
    return bytes;
}

QString sha256Text(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

Id decimalId(const QString &text, const QString &flag) {
    bool ok{};
    const auto id = text.toULongLong(&ok);
    if (!ok || !id || QString::number(id) != text)
        refuse("INVALID_REQUEST", "--" + flag + " takes a positive decimal ID");
    return Id(id);
}

// A catalog named on the command line: a file, or a catalog in the library folder by name. The
// library's own name rule (R100.d) applies, so no request reaches a path it did not name.
CatalogPackage catalogNamed(const QString &reference) {
    if (reference.contains(QLatin1Char('/')) ||
        reference.endsWith(QStringLiteral(".sketchycat"), Qt::CaseInsensitive)) {
        if (!QFileInfo(reference).isFile())
            refuse("NOT_FOUND", "No catalog file is at " + reference);
        return readingFile([&] { return loadCatalog(reference); });
    }
    if (!isCatalogLibraryName(reference))
        refuse("INVALID_REQUEST", "A catalog library name is 1 to 64 letters, digits, spaces, "
                                  "dots, underscores or hyphens, starting and ending with a "
                                  "letter, digit, underscore or hyphen");
    const auto folder = catalogLibraryFolder();
    if (folder.isEmpty() || !QFileInfo::exists(QDir(folder).filePath(reference + ".sketchycat")))
        refuse("NOT_FOUND", "No catalog named " + reference + " is in the catalog library");
    return readingFile([&] { return loadCatalogLibraryEntry(folder, reference); });
}

QJsonObject runImportStyle(const QCommandLineParser &parser) {
    const auto output = parser.value("output");
    requireNewFile(output, "output");
    auto document = readingFile([&] { return loadDocument(parser.value("input")); });
    const auto bytes = readInputFile(parser.value("import-style"), styleFileLimit, "A style file");
    const auto bundle = readingFile([&] { return decodeStyleBundle(bytes); });
    if (bundle.textureStatus == StyleTextureStatus::Missing ||
        bundle.textureStatus == StyleTextureStatus::HashMismatch) {
        const auto status =
            QString::fromLatin1(styleTextureStatusCode(bundle.textureStatus).data());
        const auto id = QString::number(bundle.texture.id);
        const auto name = QString::fromStdString(bundle.texture.name);
        refuse(
            "INPUT_ERROR",
            "Style edge texture '" + name + "' (asset " + id + ") is " +
                (bundle.textureStatus == StyleTextureStatus::Missing ? "missing"
                                                                     : "hash-mismatched") +
                " in the style file; nothing was imported and no texture was substituted",
            QJsonObject{{"texture", QJsonObject{{"id", id}, {"name", name}, {"status", status}}}});
    }
    const auto applied = readingFile([&] {
        return executeStyleCommand(document,
                                   QJsonObject{{"command", "style.apply"},
                                               {"data", QString::fromLatin1(bytes.toBase64())}});
    });
    QJsonObject result{{"status", "imported"}, {"style", applied}};
    result["nativeFile"] = createNativeFile(document, output);
    return result;
}

QJsonObject runExportStyle(const QCommandLineParser &parser) {
    const auto path = parser.value("export-style");
    requireSuffix(path, ".sketchystyle", "export-style");
    requireNewFile(path, "export-style");
    const auto document = readingFile([&] { return loadDocument(parser.value("input")); });
    const auto style = document.style();
    if (style.edgeStroke && style.edgeStroke->texture) {
        const auto &assets = document.assets();
        const auto found = assets.find(style.edgeStroke->texture);
        if (found == assets.end() || !found->second->payload)
            refuse("INPUT_ERROR",
                   "The edge texture of the document has no bytes; the style was not "
                   "exported and nothing was written");
    }
    const auto name = parser.isSet("name") ? parser.value("name") : baseName(path, ".sketchystyle");
    QJsonArray labels;
    for (const auto &label : parser.values("label"))
        labels.append(label);
    QJsonObject request{{"name", name}, {"labels", labels}};
    if (parser.isSet("description"))
        request["description"] = parser.value("description");
    const auto exported = asCode("INVALID_REQUEST", [&] { return styleExport(document, request); });
    const auto bytes = QByteArray::fromBase64(exported["data"].toString().toLatin1());
    writeStyleBundle(bytes, path);
    QJsonObject report{{"output", path},
                       {"name", name},
                       {"bytes", double(bytes.size())},
                       {"sha256", sha256Text(bytes)}};
    if (exported.contains("texture"))
        report["texture"] = exported["texture"];
    return {{"status", "exported"}, {"exportReport", report}};
}

QJsonObject runListCatalog(const QCommandLineParser &parser) {
    return encodeCatalogListing(catalogNamed(parser.value("list-catalog")));
}

QJsonObject runImportCatalog(const QCommandLineParser &parser) {
    const auto output = parser.value("output");
    requireNewFile(output, "output");
    const auto names = parser.values("entries");
    for (const auto &name : names)
        if (name.isEmpty())
            refuse("INVALID_REQUEST", "--entries needs a catalog entry name");
    const auto package = catalogNamed(parser.value("import-catalog"));
    // A name that matches no entry, or several, is a bad request, named before anything is read.
    for (const auto &name : names) {
        qsizetype matches{};
        for (const auto &entry : package.entries)
            matches += entry.name == name;
        if (matches == 0)
            refuse("INVALID_REQUEST", "No catalog entry is named '" + name + "'");
        if (matches > 1)
            refuse("INVALID_REQUEST", "Catalog entry name '" + name + "' is ambiguous");
    }
    auto document = readingFile([&] { return loadDocument(parser.value("input")); });
    const auto result = readingFile([&] { return importCatalogEntries(document, package, names); });
    auto report = catalogImportReport(package, result);
    report["status"] = "imported";
    report["nativeFile"] = createNativeFile(document, output);
    return report;
}

QJsonObject runExportCatalog(const QCommandLineParser &parser) {
    const auto target = parser.value("export-catalog");
    const bool toLibrary = parser.isSet("to-library");
    QString path, name;
    if (toLibrary) {
        if (!isCatalogLibraryName(target))
            refuse("INVALID_REQUEST", "A catalog library name is 1 to 64 letters, digits, spaces, "
                                      "dots, underscores or hyphens, starting and ending with a "
                                      "letter, digit, underscore or hyphen");
        const auto folder = catalogLibraryFolder();
        if (folder.isEmpty())
            refuse("INVALID_REQUEST", "This platform has no catalog library folder");
        path = QDir(folder).filePath(target + QStringLiteral(".sketchycat"));
        name = target;
    } else {
        requireSuffix(target, ".sketchycat", "export-catalog");
        path = target;
        name = baseName(target, ".sketchycat");
    }
    if (parser.isSet("name"))
        name = parser.value("name");
    requireNewFile(path, "export-catalog");
    CatalogExportRequest request;
    request.name = name;
    request.description = parser.value("description");
    request.version =
        parser.isSet("catalog-version") ? parser.value("catalog-version") : QStringLiteral("1.0.0");
    request.licenseIdentifier = parser.isSet("catalog-license")
                                    ? parser.value("catalog-license")
                                    : QStringLiteral("LicenseRef-Unspecified");
    request.labels = parser.values("label");
    for (const auto &id : parser.values("catalog-component"))
        request.components.push_back(decimalId(id, "catalog-component"));
    for (const auto &id : parser.values("catalog-material"))
        request.materials.push_back(decimalId(id, "catalog-material"));
    request.style = parser.isSet("catalog-style");
    const auto document = readingFile([&] { return loadDocument(parser.value("input")); });
    const auto bytes = asCode("INVALID_REQUEST", [&] { return exportCatalog(document, request); });
    const auto package = readingFile([&] { return decodeCatalog(bytes); });
    if (toLibrary && !QDir().mkpath(catalogLibraryFolder()))
        refuse("INVALID_OPERATION", "Could not create the catalog library folder");
    writeCatalog(bytes, path);
    QJsonObject report{{"output", path},
                       {"name", name},
                       {"catalogId", package.metadata.catalogId},
                       {"entryCount", int(package.entries.size())},
                       {"bytes", double(bytes.size())},
                       {"sha256", sha256Text(bytes)}};
    if (toLibrary)
        report["library"] = name;
    return {{"status", "exported"}, {"exportReport", report}};
}

QJsonArray stringArray(const QStringList &values) { return QJsonArray::fromStringList(values); }

QJsonObject optionJson(const StyleCatalogOption &option) {
    QJsonObject record{{"flag", "--" + option.name},
                       {"summary", option.summary},
                       {"repeatable", option.repeatable}};
    if (option.argument.isEmpty())
        record["switch"] = true;
    else
        record["argument"] = option.argument;
    return record;
}

// The shared options are described here, because main registers them with the other operations.
QJsonObject sharedOption(const QString &name) {
    if (name == QStringLiteral("input"))
        return {{"flag", "--input"},
                {"argument", "path"},
                {"summary", "The native model to read. It is never changed."}};
    return {{"flag", "--output"},
            {"argument", "path"},
            {"summary", "The new native file to write. It must not exist."}};
}

QJsonObject exitCodes() {
    return {
        {"0", "The operation completed; one JSON result on stdout."},
        {"1", "Refused; one JSON failure {status, code, error} on stderr. Nothing was published."}};
}

} // namespace

QStringList styleCatalogOperations() {
    return {"import-style", "export-style", "list-catalog", "import-catalog", "export-catalog"};
}

const std::vector<StyleCatalogOption> &styleCatalogOptions() {
    static const std::vector<StyleCatalogOption> options{
        {"import-style", "path",
         "Import: apply this .sketchystyle file to --input and write --output (an operation)"},
        {"export-style", "path",
         "Export: write the document style of --input to this new .sketchystyle file (an "
         "operation)"},
        {"list-catalog", "file or name",
         "List: print the catalog.list result of a .sketchycat file or a library catalog (an "
         "operation)"},
        {"import-catalog", "file or name",
         "Import: take the --entries of a .sketchycat file or a library catalog into --input (an "
         "operation)"},
        {"export-catalog", "file or name",
         "Export: write the chosen records of --input to a new .sketchycat file, or to the library "
         "with --to-library (an operation)"},
        {"entries", "name", "Import: a catalog entry to import; repeat for each entry", true},
        {"name", "name", "Export: the style or catalog name (default: the file's base name)"},
        {"description", "text", "Export: the style or catalog description"},
        {"label", "text", "Export: a search label; repeat for several", true},
        {"catalog-version", "version", "Export catalog: the catalog version (default 1.0.0)"},
        {"catalog-license", "identifier",
         "Export catalog: the licence identifier (default LicenseRef-Unspecified)"},
        {"catalog-component", "id",
         "Export catalog: a component definition ID to include; repeat for several", true},
        {"catalog-material", "id", "Export catalog: a material ID to include; repeat for several",
         true},
        {"catalog-style", "", "Export catalog: include the document style"},
        {"to-library", "",
         "Export catalog: write NAME.sketchycat into the catalog library folder instead of a path"},
    };
    return options;
}

QString styleCatalogOperationOf(const QCommandLineParser &parser) {
    QString chosen;
    for (const auto &operation : styleCatalogOperations())
        if (parser.isSet(operation)) {
            if (!chosen.isEmpty())
                refuse("INVALID_REQUEST", "Choose one style or catalog operation");
            chosen = operation;
        }
    return chosen;
}

QJsonObject runStyleCatalogCli(const QCommandLineParser &parser, const QString &operation) {
    const auto *spec = findOperation(operation);
    if (!spec)
        refuse("INVALID_REQUEST", "Unknown style or catalog operation");
    for (const auto &name : parser.optionNames())
        if (name != operation && !spec->options.contains(name))
            refuse("INVALID_REQUEST", "--" + operation + " does not take --" + name);
    if (!parser.positionalArguments().isEmpty())
        refuse("INVALID_REQUEST", "Style and catalog operations take named paths only");
    for (const auto &name : spec->required)
        if (!parser.isSet(name))
            refuse("INVALID_REQUEST", "--" + operation + " requires --" + name);
    if (operation == QStringLiteral("import-style"))
        return runImportStyle(parser);
    if (operation == QStringLiteral("export-style"))
        return runExportStyle(parser);
    if (operation == QStringLiteral("list-catalog"))
        return runListCatalog(parser);
    if (operation == QStringLiteral("import-catalog"))
        return runImportCatalog(parser);
    return runExportCatalog(parser);
}

QJsonObject styleCatalogCliCapabilities() {
    QJsonObject operations;
    for (const auto &spec : operationSpecs())
        operations[spec.name] = QJsonObject{{"flag", "--" + spec.name},
                                            {"summary", spec.summary},
                                            {"requires", stringArray(spec.required)},
                                            {"options", stringArray(spec.options)}};
    QJsonObject options;
    for (const auto &option : styleCatalogOptions())
        options[option.name] = optionJson(option);
    return {{"operations", operations},
            {"options", options},
            {"shared",
             QJsonObject{{"input", sharedOption("input")}, {"output", sharedOption("output")}}},
            {"publishes", "New files only. An existing path or a link is refused, and nothing is "
                          "published on failure."},
            {"catalogLibrary", "AppDataLocation/catalogs: the catalog library folder, named by "
                               "1 to 64 letters, digits, spaces, dots, underscores or hyphens"},
            {"exitCodes", exitCodes()}};
}

QJsonObject styleCatalogCliDescription(const QString &operation) {
    const auto *spec = findOperation(operation);
    if (!spec)
        return {};
    QJsonObject options;
    for (const auto &name : spec->options) {
        if (name == QStringLiteral("input") || name == QStringLiteral("output")) {
            options[name] = sharedOption(name);
            continue;
        }
        for (const auto &option : styleCatalogOptions())
            if (option.name == name)
                options[name] = optionJson(option);
    }
    return {{"name", spec->name},
            {"flag", "--" + spec->name},
            {"usage", spec->usage},
            {"summary", spec->summary},
            {"requires", stringArray(spec->required)},
            {"options", options},
            {"rules", stringArray(spec->rules)},
            {"exitCodes", exitCodes()}};
}

} // namespace sketchy
