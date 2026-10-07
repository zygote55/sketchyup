#include "automation/commands.hpp"
#include "automation/extension.hpp"
#include "automation/inspection.hpp"
#include "automation/local_mcp.hpp"
#include "automation/mcp.hpp"
#include "automation/recipe.hpp"
#include "automation/session.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include "io/dxf_export.hpp"
#include "io/formline.hpp"
#include "io/gltf_import.hpp"
#include "io/measured_request.hpp"
#include "io/native_format.hpp"
#include "io/obj_export.hpp"
#include "io/recovery.hpp"
#include "io/stl_export.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <iostream>
int main(int argc, char **argv) {
    bool measuredExport = false;
    for (int i = 1; i < argc; ++i) {
        const auto argument = QByteArray(argv[i]);
        measuredExport =
            measuredExport || argument == "--export-view" || argument.startsWith("--export-view=");
    }
    QStringList originalArguments;
    for (int i = 0; i < argc; ++i)
        originalArguments.append(QString::fromLocal8Bit(argv[i]));
    int guiArgc = 1;
    char *guiArgv[]{argv[0], nullptr};
    std::unique_ptr<QCoreApplication> application;
    if (measuredExport) {
        // Explicit font-using export runs without a display or user desktop plugins.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
        qputenv("QT_IM_MODULE", "compose");
        application = std::make_unique<QGuiApplication>(guiArgc, guiArgv);
    } else
        application = std::make_unique<QCoreApplication>(argc, argv);
    auto &app = *application;
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {"extension-capabilities", "Describe versioned declarative extension capabilities"});
    parser.addOption(
        {"export-view", "Export a measured orthographic PDF or SVG to a new file", "path"});
    parser.addOption(
        {"view-settings", "Read explicit measured page, scale, mode and camera settings", "path"});
    parser.addOption({"format-capabilities", "Describe supported native storage and migration"});
    parser.addOption({"inspect-native", "Validate and describe an explicit native file", "path"});
    parser.addOption({"validate-native", "Fully validate an explicit native file", "path"});
    parser.addOption(
        {"migrate-native", "Migrate an explicit native file to a new --output path", "path"});
    parser.addOption({"mcp-connect",
                      "Bridge stdio to an explicitly launched native inspection socket", "socket"});
    parser.addOption({"mcp", "Run the explicitly scoped local MCP stdio server"});
    parser.addOption({"mcp-capabilities", "Print supported MCP version, tools and bounds"});
    parser.addOption({"recipe", "Run a versioned transaction recipe", "path"});
    parser.addOption({"recipe-capabilities", "Print recipe schema and reference rules"});
    parser.addOption({"session", "Run a persistent bounded JSON-lines automation session"});
    parser.addOption(
        {"new", "Create a new model at the explicit output before starting a session"});
    parser.addOption({"outcomes", "Private durable transaction outcome directory", "directory"});
    parser.addOption(
        {"recover-latest", "Explicitly recover the latest durable transaction in this session"});
    parser.addOption({"session-capabilities", "Print the bounded session and transaction schemas"});
    parser.addOption(
        {"export-glb", "Export an immutable scene and manifest to a new directory", "directory"});
    parser.addOption({"render-settings", "Read versioned camera and render settings", "path"});
    parser.addOption({"capabilities", "Print supported local commands"});
    parser.addOption({"describe-command", "Print a command parameter schema", "name"});
    parser.addOption({"query", "Run a read-only document or geometry query", "name"});
    parser.addOption({"query-file", "Run a read-only query object from a JSON file", "path"});
    parser.addOption({"inspect", "Run a bounded query using the explicit input document", "name"});
    parser.addOption({"inspect-file", "Run a versioned bounded inspection request", "path"});
    parser.addOption(
        {"import-dxf", "Import the supported 2D DXF subset; optional new native --output", "path"});
    parser.addOption({"export-dxf", "Export world-XY model edges to a new DXF file", "path"});
    parser.addOption(
        {"dxf-unit", "Explicit unit: mm, cm, m, in, ft, or header for import", "unit"});
    parser.addOption({"dxf-segments", "Import chord segments per full circle: 12..256", "count"});
    parser.addOption({"context", "Body context for geometry.inspect", "id"});
    parser.addOption({"input", "Open a model", "path"});
    parser.addOption(
        {"import-gltf", "Import GLB/glTF into a new native model; optional new --output", "path"});
    parser.addOption(
        {"import-stl", "Import binary/ASCII STL; optional new native --output", "path"});
    parser.addOption(
        {"export-stl", "Export all model surface triangles to a new STL file", "path"});
    parser.addOption({"stl-unit", "Explicit STL coordinate unit: mm, cm, m, in or ft", "unit"});
    parser.addOption({"stl-up", "Explicit STL up axis: y or z", "axis"});
    parser.addOption(
        {"stl-weld", "Required import welding choice: none, exact or tolerance", "mode"});
    parser.addOption(
        {"stl-tolerance", "Metres, required only with tolerance welding: 1e-9..1e-3", "metres"});
    parser.addOption(
        {"stl-discard-degenerate", "Explicitly remove collapsed facets during import"});
    parser.addOption({"stl-encoding", "Required export encoding: binary or ascii", "encoding"});
    parser.addOption({"import-obj", "Import OBJ/MTL; optional new native --output", "path"});
    parser.addOption(
        {"export-obj", "Export all model geometry to a new OBJ package directory", "directory"});
    parser.addOption({"obj-unit", "Explicit OBJ coordinate unit: mm, cm, m, in or ft", "unit"});
    parser.addOption({"obj-up", "Explicit OBJ up axis: y or z", "axis"});
    parser.addOption({"import-formline", "Import Formline v1 into a new native model", "path"});
    parser.addOption(
        {"recovery-list", "List verified inactive recovery sessions in a directory", "directory"});
    parser.addOption(
        {"recover", "Open a verified recovery session directory as an unsaved copy", "directory"});
    parser.addOption(
        {"history-position", "Move retained history cursor after input or script", "position"});
    parser.addOption({"output", "Save the resulting model", "path"});
    parser.addOption(
        {"preview", "Validate a script and return prospective geometry without committing"});
    parser.addOption({"script", "Read a command array from a local JSON file", "path"});
    try {
        // Parse without QCommandLineParser's unstructured error exit.
        if (!parser.parse(measuredExport ? originalArguments : app.arguments()))
            throw sketchy::InspectionError("INVALID_REQUEST", parser.errorText().toStdString());
        if (parser.isSet("help"))
            parser.showHelp();
        if (parser.isSet("extension-capabilities")) {
            if (parser.optionNames().size() != 1 || !parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Extension discovery is a standalone operation");
            std::cout << QJsonDocument(sketchy::extensionCapabilities())
                             .toJson(QJsonDocument::Compact)
                             .toStdString()
                      << '\n';
            return 0;
        }
        if (parser.isSet("export-view")) {
            const QStringList allowed{"export-view", "view-settings", "input"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "Measured export is a standalone operation");
            if (!parser.positionalArguments().isEmpty() || !parser.isSet("input") ||
                !parser.isSet("view-settings"))
                throw sketchy::InspectionError(
                    "INVALID_REQUEST", "Measured export requires --input and --view-settings");
            QFile settings(parser.value("view-settings"));
            if (!settings.open(QIODevice::ReadOnly) || settings.size() > 32768)
                throw sketchy::InspectionError(
                    "INVALID_REQUEST",
                    "Measured settings must be a readable file no larger than 32 KiB");
            const auto bytes = settings.read(32769);
            QJsonParseError parse;
            const auto json = QJsonDocument::fromJson(bytes, &parse);
            if (bytes.size() > 32768 || settings.error() != QFileDevice::NoError ||
                parse.error != QJsonParseError::NoError || !json.isObject())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Invalid measured export settings JSON");
            const auto request = sketchy::parseMeasuredRequest(json.object());
            const auto document = sketchy::loadDocument(parser.value("input"));
            const auto drawing = sketchy::captureMeasuredDrawing(
                sketchy::RenderSnapshot::capture(document, request.render), request.page);
            const auto output = sketchy::exportMeasuredDrawing(drawing, request.format);
            sketchy::writeMeasuredExport(output, parser.value("export-view"));
            std::cout << QJsonDocument(
                             QJsonObject{{"status", "exported"}, {"exportReport", output.report}})
                             .toJson(QJsonDocument::Compact)
                             .toStdString()
                      << '\n';
            return 0;
        }
        if (parser.isSet("view-settings"))
            throw sketchy::InspectionError("INVALID_REQUEST",
                                           "--view-settings requires --export-view");
        if (parser.isSet("import-dxf") || parser.isSet("export-dxf")) {
            auto invalid = [](const char *message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            const bool importing = parser.isSet("import-dxf");
            if (importing == parser.isSet("export-dxf") ||
                !parser.positionalArguments().isEmpty() || !parser.isSet("dxf-unit"))
                invalid("Choose one DXF operation and explicit --dxf-unit");
            const QStringList allowed =
                importing ? QStringList{"import-dxf", "output", "dxf-unit", "dxf-segments"}
                          : QStringList{"export-dxf", "input", "dxf-unit"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("DXF commands are standalone operations");
            const std::map<QString, double> units{
                {"mm", .001}, {"cm", .01}, {"m", 1}, {"in", .0254}, {"ft", .3048}};
            const auto unit = parser.value("dxf-unit");
            sketchy::DxfOptions options;
            if (units.contains(unit))
                options.metresPerUnit = units.at(unit);
            else if (!importing || unit != "header")
                invalid("DXF units are mm, cm, m, in, ft, or header for import");
            QJsonObject result;
            if (importing) {
                unsigned segments = 96;
                if (parser.isSet("dxf-segments")) {
                    bool ok{};
                    segments = parser.value("dxf-segments").toUInt(&ok);
                    if (!ok || segments < 12 || segments > 256)
                        invalid("DXF segments per full circle must be 12..256");
                }
                const auto imported =
                    sketchy::loadDxf(parser.value("import-dxf"), options, segments);
                result = {{"status", "imported"}, {"importReport", imported.report}};
                if (parser.isSet("output"))
                    result["nativeFile"] =
                        sketchy::createNativeFile(imported.document, parser.value("output"));
            } else {
                if (!parser.isSet("input"))
                    invalid("DXF export requires native --input");
                const auto document = sketchy::loadDocument(parser.value("input"));
                const auto exported = sketchy::exportDxf(document, *options.metresPerUnit);
                sketchy::writeDxfExport(exported, parser.value("export-dxf"));
                result = {{"status", "exported"}, {"exportReport", exported.report}};
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        for (const auto &option : parser.optionNames())
            if (option.startsWith("dxf-"))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "DXF options require --import-dxf or --export-dxf");
        if (parser.isSet("import-stl") || parser.isSet("export-stl")) {
            auto invalid = [](const char *message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            const bool importing = parser.isSet("import-stl");
            if (importing == parser.isSet("export-stl") ||
                !parser.positionalArguments().isEmpty() || !parser.isSet("stl-unit") ||
                !parser.isSet("stl-up"))
                invalid("Choose one STL operation with explicit --stl-unit and --stl-up");
            const QStringList allowed = importing ? QStringList{"import-stl",
                                                                "output",
                                                                "stl-unit",
                                                                "stl-up",
                                                                "stl-weld",
                                                                "stl-tolerance",
                                                                "stl-discard-degenerate"}
                                                  : QStringList{"export-stl", "input", "stl-unit",
                                                                "stl-up", "stl-encoding"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("STL commands are standalone operations");
            const std::map<QString, double> units{
                {"mm", .001}, {"cm", .01}, {"m", 1}, {"in", .0254}, {"ft", .3048}};
            const auto unit = parser.value("stl-unit"), up = parser.value("stl-up");
            if (!units.contains(unit) || (up != "y" && up != "z"))
                invalid("STL units are mm, cm, m, in or ft; up axis is y or z");
            const sketchy::StlCoordinateOptions coordinates{
                units.at(unit), up == "y" ? sketchy::StlUpAxis::Y : sketchy::StlUpAxis::Z};
            QJsonObject result;
            if (importing) {
                const auto weld = parser.value("stl-weld");
                if (weld != "none" && weld != "exact" && weld != "tolerance")
                    invalid("Import requires --stl-weld none, exact or tolerance");
                if ((weld == "tolerance") != parser.isSet("stl-tolerance"))
                    invalid("--stl-tolerance is required only for tolerance welding");
                sketchy::StlRepairOptions repairs{weld == "none"    ? sketchy::StlWeld::None
                                                  : weld == "exact" ? sketchy::StlWeld::Exact
                                                                    : sketchy::StlWeld::Tolerance,
                                                  1e-7, parser.isSet("stl-discard-degenerate")};
                if (weld == "tolerance") {
                    bool ok{};
                    repairs.toleranceMetres = parser.value("stl-tolerance").toDouble(&ok);
                    if (!ok)
                        invalid("Invalid STL weld tolerance");
                }
                repairs.validate();
                const auto imported =
                    sketchy::loadStl(parser.value("import-stl"), coordinates, repairs);
                result = {{"status", "imported"}, {"importReport", imported.report}};
                if (parser.isSet("output"))
                    result["nativeFile"] =
                        sketchy::createNativeFile(imported.document, parser.value("output"));
            } else {
                const auto encoding = parser.value("stl-encoding");
                if (!parser.isSet("input") || (encoding != "binary" && encoding != "ascii"))
                    invalid("STL export requires --input and --stl-encoding binary or ascii");
                const auto document = sketchy::loadDocument(parser.value("input"));
                const auto exported =
                    sketchy::exportStl(document, coordinates,
                                       encoding == "binary" ? sketchy::StlEncoding::Binary
                                                            : sketchy::StlEncoding::Ascii);
                sketchy::writeStlExport(exported, parser.value("export-stl"));
                result = {{"status", "exported"}, {"exportReport", exported.report}};
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        for (const auto &option : parser.optionNames())
            if (option.startsWith("stl-"))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "STL options require --import-stl or --export-stl");
        if (parser.isSet("import-obj") || parser.isSet("export-obj")) {
            const bool importing = parser.isSet("import-obj");
            if (importing == parser.isSet("export-obj") ||
                !parser.positionalArguments().isEmpty() || !parser.isSet("obj-unit") ||
                !parser.isSet("obj-up"))
                throw sketchy::InspectionError(
                    "INVALID_REQUEST",
                    "Choose one OBJ operation with explicit --obj-unit and --obj-up");
            const QStringList allowed =
                importing ? QStringList{"import-obj", "output", "obj-unit", "obj-up"}
                          : QStringList{"export-obj", "input", "obj-unit", "obj-up"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "OBJ commands are standalone operations");
            const std::map<QString, double> units{
                {"mm", .001}, {"cm", .01}, {"m", 1}, {"in", .0254}, {"ft", .3048}};
            const auto unit = parser.value("obj-unit"), up = parser.value("obj-up");
            if (!units.contains(unit) || (up != "y" && up != "z"))
                throw sketchy::InspectionError(
                    "INVALID_REQUEST", "OBJ units are mm, cm, m, in or ft; up axis is y or z");
            const sketchy::ObjImportOptions options{
                units.at(unit), up == "y" ? sketchy::ObjUpAxis::Y : sketchy::ObjUpAxis::Z};
            QJsonObject result;
            if (importing) {
                const auto imported = sketchy::loadObj(parser.value("import-obj"), options);
                result = {{"status", "imported"}, {"importReport", imported.report}};
                if (parser.isSet("output"))
                    result["nativeFile"] =
                        sketchy::createNativeFile(imported.document, parser.value("output"));
            } else {
                if (!parser.isSet("input"))
                    throw sketchy::InspectionError(
                        "INVALID_REQUEST", "OBJ export requires an explicit native --input");
                const auto document = sketchy::loadDocument(parser.value("input"));
                const auto package = sketchy::exportObj(document, options);
                sketchy::writeObjExport(package, parser.value("export-obj"));
                result = {{"status", "exported"}, {"manifest", package.manifest}};
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        if (parser.isSet("obj-unit") || parser.isSet("obj-up"))
            throw sketchy::InspectionError("INVALID_REQUEST",
                                           "OBJ options require --import-obj or --export-obj");
        const QStringList formatModes{"format-capabilities", "inspect-native", "validate-native",
                                      "migrate-native"};
        QString formatMode;
        for (const auto &name : formatModes)
            if (parser.isSet(name)) {
                if (!formatMode.isEmpty())
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "Choose one native format operation");
                formatMode = name;
            }
        if (!formatMode.isEmpty()) {
            if (!parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Native format commands require named paths");
            for (const auto &name : parser.optionNames())
                if (name != formatMode && !(formatMode == "migrate-native" && name == "output"))
                    throw sketchy::InspectionError(
                        "INVALID_REQUEST", "Native format commands are standalone operations");
            if (formatMode == "migrate-native" && !parser.isSet("output"))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Migration requires a new --output path");
            const auto report =
                formatMode == "format-capabilities" ? sketchy::nativeFormatCapabilities()
                : formatMode == "migrate-native"
                    ? sketchy::migrateNativeFile(parser.value(formatMode), parser.value("output"))
                    : sketchy::inspectNativeFile(parser.value(formatMode));
            std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        if (parser.isSet("import-gltf")) {
            if (!parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "glTF import requires named paths");
            for (const auto &option : parser.optionNames())
                if (option != "import-gltf" && option != "output")
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "glTF import is a standalone operation");
            const auto imported = sketchy::loadGltf(parser.value("import-gltf"));
            QJsonObject result{{"status", "imported"}, {"importReport", imported.report}};
            if (parser.isSet("output"))
                result["nativeFile"] =
                    sketchy::createNativeFile(imported.document, parser.value("output"));
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        if (parser.isSet("export-glb")) {
            if (!parser.isSet("input") || !parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "GLB export requires an explicit input model");
            for (const auto &option : parser.optionNames())
                if (option != "input" && option != "export-glb" && option != "render-settings")
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "GLB export is a standalone operation");
            sketchy::RenderOptions options;
            if (parser.isSet("render-settings")) {
                QFile file(parser.value("render-settings"));
                if (!QFileInfo(file.fileName()).isFile() || !file.open(QIODevice::ReadOnly) ||
                    file.size() > 16 * 1024)
                    throw sketchy::InspectionError(
                        "INPUT_ERROR", "Cannot read render settings or settings exceed 16 KiB");
                const auto bytes = file.read(16 * 1024 + 1);
                QJsonParseError error;
                const auto json = QJsonDocument::fromJson(bytes, &error);
                if (bytes.size() > 16 * 1024 || error.error != QJsonParseError::NoError ||
                    !json.isObject())
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "Expected bounded render settings JSON");
                options = sketchy::parseRenderOptions(json.object());
            }
            const auto document = sketchy::loadDocument(parser.value("input"));
            const auto exported =
                sketchy::exportGlb(sketchy::RenderSnapshot::capture(document, options));
            sketchy::writeGlbExport(exported, parser.value("export-glb"));
            std::cout
                << QJsonDocument(exported.manifest).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
            return 0;
        }
        if (parser.isSet("render-settings"))
            throw sketchy::InspectionError("INVALID_REQUEST",
                                           "Render settings require --export-glb");
        if (parser.isSet("mcp-connect")) {
            if (parser.optionNames().size() != 1 || !parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "MCP connection is a standalone mode");
            return sketchy::bridgeMcpStdio(parser.value("mcp-connect"));
        }
        if (parser.isSet("mcp") || parser.isSet("mcp-capabilities") || parser.isSet("session") ||
            parser.isSet("session-capabilities") || parser.isSet("recipe") ||
            parser.isSet("recipe-capabilities")) {
            for (const auto *option :
                 {"capabilities", "describe-command", "query", "query-file", "inspect",
                  "inspect-file", "context", "import-formline", "recovery-list", "recover",
                  "history-position", "preview", "script"})
                if (parser.isSet(option))
                    throw sketchy::InspectionError(
                        "INVALID_REQUEST",
                        "Session mode cannot be combined with legacy model operations");
            if (!parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Unexpected positional arguments");
            if (int(parser.isSet("mcp")) + int(parser.isSet("mcp-capabilities")) +
                    int(parser.isSet("session")) + int(parser.isSet("session-capabilities")) +
                    int(parser.isSet("recipe")) + int(parser.isSet("recipe-capabilities")) !=
                1)
                throw sketchy::InspectionError(
                    "INVALID_REQUEST", "Choose exactly one session, recipe or discovery mode");
            if (parser.isSet("mcp-capabilities") || parser.isSet("session-capabilities") ||
                parser.isSet("recipe-capabilities")) {
                for (const auto *option :
                     {"session", "input", "output", "new", "outcomes", "recover-latest"})
                    if (parser.isSet(option))
                        throw sketchy::InspectionError(
                            "INVALID_REQUEST", "Session discovery is a standalone operation");
                std::cout << QJsonDocument(parser.isSet("mcp-capabilities")
                                               ? sketchy::mcpCapabilities()
                                           : parser.isSet("recipe-capabilities")
                                               ? sketchy::recipeCapabilities()
                                               : sketchy::sessionCapabilities())
                                 .toJson()
                                 .toStdString();
                return 0;
            }
            std::optional<sketchy::AutomationRecipe> recipe;
            if (parser.isSet("recipe"))
                recipe = sketchy::AutomationRecipe::load(parser.value("recipe"));
            sketchy::AutomationSession session({parser.value("input"), parser.value("output"),
                                                parser.value("outcomes"), parser.isSet("new"),
                                                parser.isSet("recover-latest")});
            QFile input, output;
            if (!input.open(stdin, QIODevice::ReadOnly) ||
                !output.open(stdout, QIODevice::WriteOnly))
                throw sketchy::InspectionError("INPUT_ERROR", "Cannot open automation streams");
            if (parser.isSet("mcp")) {
                sketchy::McpServer server(session);
                return sketchy::runMcpStream(server, input, output);
            }
            return recipe ? recipe->run(session, output)
                          : sketchy::runAutomationStream(session, input, output);
        }
        for (const auto *option : {"new", "outcomes", "recover-latest"})
            if (parser.isSet(option))
                throw sketchy::InspectionError(
                    "INVALID_REQUEST", "Session options require --session, --recipe or --mcp");
        if (parser.isSet("inspect") || parser.isSet("inspect-file")) {
            if (!parser.isSet("input") || (parser.isSet("inspect") && parser.isSet("inspect-file")))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Choose one inspection mode with --input");
            for (const auto *option : {"capabilities", "describe-command", "query", "query-file",
                                       "context", "import-formline", "recovery-list", "recover",
                                       "history-position", "output", "preview", "script"})
                if (parser.isSet(option))
                    throw sketchy::InspectionError(
                        "INVALID_REQUEST", "Inspection cannot be combined with other operations");
            const auto document = sketchy::loadDocument(parser.value("input"));
            QJsonObject request;
            if (parser.isSet("inspect-file")) {
                QFile file(parser.value("inspect-file"));
                if (!file.open(QIODevice::ReadOnly))
                    throw sketchy::InspectionError("INPUT_ERROR", "Cannot open inspection request");
                const auto bytes = file.read(sketchy::inspectionRequestBytes + 1);
                if (bytes.size() > sketchy::inspectionRequestBytes)
                    throw sketchy::InspectionError("LIMIT_EXCEEDED",
                                                   "Inspection request exceeds 16 KiB");
                QJsonParseError error;
                const auto json = QJsonDocument::fromJson(bytes, &error);
                if (error.error != QJsonParseError::NoError || !json.isObject())
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "Expected one JSON request object");
                request = json.object();
            } else {
                request = {{"apiVersion", 1},
                           {"documentId", QString::fromStdString(document.identity())},
                           {"expectedRevision", QString::number(document.revision())},
                           {"query", parser.value("inspect")}};
            }
            std::cout << QJsonDocument(sketchy::inspectDocument(document, request))
                             .toJson(QJsonDocument::Compact)
                             .toStdString()
                      << '\n';
            return 0;
        }
        if (parser.isSet("history-position") &&
            (parser.isSet("preview") || parser.isSet("query") || parser.isSet("query-file")))
            throw std::runtime_error(
                "History navigation cannot be combined with a query or preview");
        if (parser.isSet("preview") && (!parser.isSet("script") || parser.isSet("output")))
            throw std::runtime_error(
                "Preview requires --script and cannot be combined with --output");
        if (parser.isSet("capabilities")) {
            std::cout << QJsonDocument(sketchy::capabilities()).toJson().toStdString();
            return 0;
        }
        if (parser.isSet("describe-command")) {
            std::cout << QJsonDocument(
                             sketchy::commandDescription(parser.value("describe-command")))
                             .toJson()
                             .toStdString();
            return 0;
        }
        if (parser.isSet("recovery-list")) {
            for (const auto &option : {"input", "import-formline", "recover", "output", "script",
                                       "query", "query-file", "history-position"})
                if (parser.isSet(option))
                    throw std::runtime_error(
                        "--recovery-list cannot be combined with model operations");
            QJsonArray sessions;
            for (const auto &session : sketchy::listRecoveries(parser.value("recovery-list")))
                sessions.append(sketchy::describeRecovery(session));
            std::cout
                << QJsonDocument(QJsonObject{{"recoveries", sessions}}).toJson().toStdString();
            return 0;
        }
        if (int(parser.isSet("input")) + int(parser.isSet("import-formline")) +
                int(parser.isSet("recover")) >
            1)
            throw std::runtime_error("Choose --input, --import-formline or --recover");
        if (parser.isSet("import-formline") && parser.isSet("output") &&
            QFileInfo(parser.value("import-formline")).exists() &&
            QFileInfo(parser.value("import-formline")).canonicalFilePath() ==
                QFileInfo(parser.value("output")).canonicalFilePath())
            throw std::runtime_error("Import output must not replace the Formline source");
        QJsonObject importReport, recoveryReport;
        auto doc = parser.isSet("input") ? sketchy::loadDocument(parser.value("input"))
                                         : sketchy::Document();
        if (parser.isSet("import-formline")) {
            auto imported = sketchy::loadFormline(parser.value("import-formline"));
            doc = std::move(imported.document);
            importReport = std::move(imported.report);
        }
        if (parser.isSet("recover")) {
            const QFileInfo session(QDir(parser.value("recover")).absolutePath());
            auto recovered = sketchy::readRecovery(session.absolutePath(), session.fileName());
            if (!recovered.verified || !recovered.document)
                throw std::runtime_error(("Cannot recover: " + recovered.issue).toStdString());
            if (parser.isSet("output")) {
                const QFileInfo output(parser.value("output"));
                const auto recoveryPath = session.canonicalFilePath();
                const auto parent = output.absoluteDir().canonicalPath();
                const auto target = output.canonicalFilePath();
                if (parent == recoveryPath || parent.startsWith(recoveryPath + "/") ||
                    target.startsWith(recoveryPath + "/"))
                    throw std::runtime_error(
                        "Recovery output must be outside the recovery session");
            }
            if (parser.isSet("output") && !recovered.info.sourcePath.isEmpty()) {
                const QFileInfo source(recovered.info.sourcePath), output(parser.value("output"));
                if (source.absoluteFilePath() == output.absoluteFilePath() ||
                    (source.exists() && source.canonicalFilePath() == output.canonicalFilePath()))
                    throw std::runtime_error(
                        "Recovery output must not replace the original saved file");
            }
            recoveryReport = sketchy::describeRecovery(recovered);
            doc = std::move(*recovered.document);
        }
        QJsonObject result;
        if (int(parser.isSet("query")) + int(parser.isSet("query-file")) +
                int(parser.isSet("script")) >
            1)
            throw std::runtime_error("Choose either a query or an editing script");
        if (parser.isSet("query-file")) {
            QFile file(parser.value("query-file"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read query or query exceeds 1 MiB");
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(file.read(1024 * 1024 + 1), &error);
            if (error.error != QJsonParseError::NoError || !json.isObject())
                throw std::runtime_error("Query file must contain one JSON object");
            result = sketchy::executeQuery(doc, json.object());
        } else if (parser.isSet("query")) {
            QJsonObject query{{"query", parser.value("query")}};
            if (parser.isSet("context"))
                query["body"] = parser.value("context");
            result = sketchy::executeQuery(doc, query);
        } else if (parser.isSet("script")) {
            QFile file(parser.value("script"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read recipe or recipe exceeds 1 MiB");
            QJsonParseError error;
            auto json = QJsonDocument::fromJson(file.read(1024 * 1024 + 1), &error);
            if (error.error != QJsonParseError::NoError || !json.isArray())
                throw std::runtime_error("Recipe must be a JSON command array");
            const QJsonObject batch{{"apiVersion", 1},
                                    {"documentId", QString::fromStdString(doc.identity())},
                                    {"expectedRevision", QString::number(doc.revision())},
                                    {"commands", json.array()}};
            result = parser.isSet("preview") ? sketchy::previewBatch(doc, batch)
                                             : sketchy::executeBatch(doc, batch);
        } else
            result = sketchy::describe(doc);
        if (parser.isSet("history-position")) {
            const auto navigation = sketchy::executeHistory(
                doc, {{"apiVersion", 1},
                      {"documentId", QString::fromStdString(doc.identity())},
                      {"expectedRevision", QString::number(doc.revision())},
                      {"position", parser.value("history-position")}});
            result = sketchy::describe(doc);
            result["historyNavigation"] = navigation;
        }
        if (!recoveryReport.isEmpty())
            result["recoveryReport"] = recoveryReport;
        if (!importReport.isEmpty())
            result["importReport"] = importReport;
        if (parser.isSet("output")) {
            sketchy::saveDocument(doc, parser.value("output"));
            result["saved"] = parser.value("output");
        }
        std::cout << QJsonDocument(result).toJson().toStdString();
        return 0;
    } catch (const sketchy::InspectionError &e) {
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", QString::fromStdString(e.code())},
                                               {"error", e.what()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    } catch (const sketchy::PlanarError &e) {
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", QString::fromStdString(e.code())},
                                               {"error", e.what()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    } catch (const std::exception &e) {
        const auto failure = sketchy::automationFailure(e);
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", failure["code"]},
                                               {"error", failure["message"]}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    }
}
