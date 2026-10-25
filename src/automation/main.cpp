#include "automation/commands.hpp"
#include "automation/extension.hpp"
#include "automation/inspection.hpp"
#include "automation/local_mcp.hpp"
#include "automation/mcp.hpp"
#include "automation/optional_capabilities.hpp"
#include "automation/recipe.hpp"
#include "automation/session.hpp"
#include "automation/terrain_commands.hpp"
#include "integrations/glb_export.hpp"
#include "io/assembly_source.hpp"
#include "io/document_io.hpp"
#include "io/dxf_export.hpp"
#include "io/formline.hpp"
#include "io/gltf_import.hpp"
#include "io/ifc_export.hpp"
#include "io/step_export.hpp"
#include "io/ifc_worker.hpp"
#include "io/measured_request.hpp"
#include "io/new_file.hpp"
#include "io/native_format.hpp"
#include "io/obj_export.hpp"
#include "io/recovery.hpp"
#include "io/sheet_export.hpp"
#include "io/skp_worker.hpp"
#include "io/step_worker.hpp"
#include "io/stl_export.hpp"
#include "sketchyup/version.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QGuiApplication>
#include <QJsonDocument>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
    bool measuredExport = false;
    for (int i = 1; i < argc; ++i) {
        const auto argument = QByteArray(argv[i]);
        measuredExport = measuredExport || argument == "--export-view" ||
                         argument.startsWith("--export-view=") || argument == "--export-sheets" ||
                         argument.startsWith("--export-sheets=");
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
    QCoreApplication::setApplicationVersion(SKETCHYUP_VERSION);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"optional-capabilities",
                      "Inspect optional dependency presence without executing helpers"});
    parser.addOption(
        {"extension-capabilities", "Describe versioned declarative extension capabilities"});
    parser.addOption(
        {"export-view", "Export a measured orthographic PDF or SVG to a new file", "path"});
    parser.addOption(
        {"view-settings", "Read explicit measured page, scale, mode and camera settings", "path"});
    parser.addOption(
        {"export-sheets", "Export drawing sheets at actual size to a new PDF file", "path"});
    parser.addOption(
        {"pages", "Sheet export pages: all, A-101, 3, A-101..A-301 or a list", "range"});
    parser.addOption({"allow-broken", "Export broken sheet views as empty, labelled frames"});
    parser.addOption({"allow-changed", "Export sheet views changed since accepted as they are"});
    parser.addOption({"preflight-only", "Print sheet export preflight findings without exporting"});
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
    parser.addOption({"dxf-site-coordinates",
                      "DXF export: write site coordinates (the site origin added back); needs a "
                      "model with a site origin"});
    parser.addOption({"dxf-segments", "Import chord segments per full circle: 12..256", "count"});
    parser.addOption({"import-terrain",
                      "Import a terrain from a DXF or point file into --input (or a new model); "
                      "optional new native --output; stage progress on stderr when it is a "
                      "terminal",
                      "path"});
    parser.addOption({"terrain-units",
                      "Terrain source unit: mm, cm, m, in, ft, usft, or header for DXF", "unit"});
    parser.addOption(
        {"terrain-format", "Terrain source format: dxf or points (default: by extension)", "format"});
    parser.addOption({"origin", "Explicit site origin in metres: E,N[,Z]", "E,N[,Z]"});
    parser.addOption({"terrain-crs", "Informational CRS label for a new site origin", "label"});
    parser.addOption({"terrain-columns",
                      "Point columns: PNEZD, PENZD, ENZ, XYZ or a comma list of point, easting, "
                      "northing, elevation, description, ignore",
                      "columns"});
    parser.addOption(
        {"terrain-delimiter", "Point delimiter: comma, tab, semicolon or whitespace", "delimiter"});
    parser.addOption({"terrain-header", "Point file header row: yes or no", "yes|no"});
    parser.addOption({"terrain-roles",
                      "Roles by layer or code: NAME=ROLE[,NAME=ROLE...] with roles auto, contour, "
                      "breakline, boundary, hole, ground, ignore",
                      "roles"});
    parser.addOption({"terrain-name", "Name of the imported terrain", "name"});
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
    parser.addOption({"import-ifc",
                      "Import an IFC STEP file (IFC2X3, IFC4, IFC4X3_ADD2) through the isolated IFC helper; "
                      "optional new native --output; without it, validate and print the report",
                      "path"});
    parser.addOption({"ifc-deflection", "IFC import: maximum chord deviation of curved geometry in mm (default 1)", "mm"});
    parser.addOption({"ifc-length-unit",
                      "IFC import: length unit mm, cm, m, in or ft, used only when the file declares none", "unit"});
    parser.addOption({"ifc-spaces", "IFC import: spaces hidden (default), shown or skip", "mode"});
    parser.addOption({"ifc-georef",
                      "IFC import: keep (default) leaves the project axes and reports the map conversion; grid "
                      "rotates the model to the map grid and sets the site origin",
                      "mode"});
    parser.addOption({"export-ifc",
                      "Export the --input model to a new IFC4 file (tessellated geometry, no certification claim) "
                      "through the isolated IFC helper",
                      "path"});
    parser.addOption({"ifc-timestamp",
                      "IFC export: the time stamped into the file, UTC seconds since the epoch or YYYY-MM-DDTHH:MM:SS "
                      "(default SOURCE_DATE_EPOCH, else the clock); a fixed value makes two exports identical",
                      "time"});
    parser.addOption({"ifc-deadline", "IFC import and export: seconds the helper may run per attempt (default 120)", "seconds"});
    parser.addOption({"ifc-worker", "IFC import and export: path of the helper (development and tests)", "path"});
    // R098.a3 (ADR 0172 section 8): the STEP import's own options; the helper is the IFC
    // helper's, so its path and deadline are its own names, and the tessellation's policy
    // (section 3) and the unit for a file that declares none (section 2) are named options.
    parser.addOption({"import-step",
                      "Import a STEP file (AP203, AP214, AP242) through the isolated helper; "
                      "optional new native --output; without it, validate and print the report",
                      "path"});
    parser.addOption({"step-deflection-mm",
                      "STEP import: maximum chord deviation of curved geometry in mm (default 1)", "mm"});
    parser.addOption({"step-angle-deg", "STEP import: crease angle in degrees (default 30)", "deg"});
    parser.addOption({"step-length-unit",
                      "STEP import: length unit mm, cm, m, in or ft, used only when the file declares none", "unit"});
    parser.addOption({"export-step",
                      "Export the --input model to a new STEP file (AP214 by default, AP203; AP242 is refused) through the "
                      "isolated helper",
                      "path"});
    parser.addOption({"step-schema", "STEP export: ap214 (the default) or ap203", "schema"});
    parser.addOption({"step-deadline", "STEP import and export: seconds the helper may run (default 120)", "seconds"});
    parser.addOption({"step-worker", "STEP import and export: path of the helper (development and tests)", "path"});
    // R102.c (ADR 0176 section 9): the SketchUp (.skp) import, the STEP import's own operation with
    // the SketchUp helper (sketchyup-skp-worker). The import has no options of its own beyond the
    // helper's deadline and path (the file's facts are the report's).
    parser.addOption({"import-skp",
                      "Import a SketchUp (.skp) file through the isolated sketchyup-skp-worker helper; "
                      "optional new native --output; without it, validate and print the report",
                      "path"});
    parser.addOption({"skp-deadline", "SKP import: seconds the helper may run (default 120)", "seconds"});
    parser.addOption({"skp-worker", "SKP import: path of the helper (development and tests)", "path"});
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
    parser.addOption({"build-assembly",
                      "Build an assembly source into a new .sketchylib bundle given as the one "
                      "positional argument",
                      "source"});
    try {
        // Parse without QCommandLineParser's unstructured error exit.
        if (!parser.parse(measuredExport ? originalArguments : app.arguments()))
            throw sketchy::InspectionError("INVALID_REQUEST", parser.errorText().toStdString());
        if (parser.isSet("help"))
            parser.showHelp();
        if (parser.isSet("version"))
            parser.showVersion();
        if (parser.isSet("optional-capabilities")) {
            if (parser.optionNames().size() != 1 || !parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Optional discovery is a standalone operation");
            std::cout << QJsonDocument(sketchy::optionalCapabilities()).toJson().toStdString();
            return 0;
        }
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
        if (parser.isSet("build-assembly")) {
            // ADR 0166 §6.1 (R094.f): sketchyup-cli --build-assembly <source> <output.sketchylib>
            if (parser.optionNames().size() != 1 || parser.positionalArguments().size() != 1)
                throw sketchy::InspectionError(
                    "INVALID_REQUEST",
                    "--build-assembly takes a source and one new .sketchylib output path");
            QFile source(parser.value("build-assembly"));
            if (!source.open(QIODevice::ReadOnly) || source.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read a bounded assembly source");
            const auto output = parser.positionalArguments().first();
            const auto bundle = sketchy::buildAssemblyBundle(source.readAll());
            // Checks the bundle decodes before the new file is published.
            sketchy::writeComponentBundle(bundle, output);
            std::cout << QJsonDocument(QJsonObject{{"status", "built"},
                                                   {"output", output},
                                                   {"bytes", double(bundle.size())}})
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
        if (parser.isSet("export-sheets")) {
            const QStringList allowed{"export-sheets", "input",         "pages",
                                      "allow-broken",  "allow-changed", "preflight-only"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "Sheet export is a standalone operation");
            if (!parser.positionalArguments().isEmpty() || !parser.isSet("input"))
                throw sketchy::InspectionError("INVALID_REQUEST", "Sheet export requires --input");
            const auto document = sketchy::loadDocument(parser.value("input"));
            auto print = [](std::ostream &stream, const QJsonObject &value) {
                stream << QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString() << '\n';
            };
            try {
                sketchy::SheetExportOptions options;
                if (parser.isSet("pages"))
                    options.sheets = sketchy::resolveSheetPageRange(
                        document, parser.value("pages").toStdString());
                options.allowBroken = parser.isSet("allow-broken");
                options.allowChanged = parser.isSet("allow-changed");
                if (parser.isSet("preflight-only")) {
                    print(std::cout,
                          {{"status", "preflight"},
                           {"preflight", sketchy::sheetExportPreflightJson(
                                             sketchy::preflightSheetExport(document, options))}});
                    return 0;
                }
                const auto exported =
                    sketchy::exportSheetsPdf(document, options, parser.value("export-sheets"));
                print(std::cout, {{"status", "exported"}, {"exportReport", exported.report}});
                return 0;
            } catch (const sketchy::SheetExportError &error) {
                QJsonObject failure{{"status", "failed"},
                                    {"code", QString::fromStdString(error.code())},
                                    {"error", error.what()}};
                if (error.preflight())
                    failure["preflight"] = sketchy::sheetExportPreflightJson(*error.preflight());
                print(std::cerr, failure);
                return 1;
            }
        }
        for (const auto *option : {"pages", "allow-broken", "allow-changed", "preflight-only"})
            if (parser.isSet(option))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Sheet export options require --export-sheets");
        if (parser.isSet("import-terrain")) {
            // R095.c (ADR 0168 section 5): one labelled edit on --input or a new model.
            auto invalid = [](const std::string &message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            const QStringList allowed{"import-terrain",    "terrain-units",  "terrain-format",
                                      "origin",            "terrain-crs",    "terrain-columns",
                                      "terrain-delimiter", "terrain-header", "terrain-roles",
                                      "terrain-name",      "input",          "output"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("Terrain import is a standalone operation");
            if (!parser.positionalArguments().isEmpty())
                invalid("Unexpected positional arguments");
            const auto path = parser.value("import-terrain");
            QJsonObject command{{"command", "terrain.import"}};
            command["format"] = parser.isSet("terrain-format")
                                    ? parser.value("terrain-format")
                                    : (path.endsWith(".dxf", Qt::CaseInsensitive) ? "dxf" : "points");
            if (parser.isSet("terrain-units"))
                command["units"] = parser.value("terrain-units");
            else if (command["format"] == "points")
                invalid("Point files have no units; choose --terrain-units");
            if (parser.isSet("origin")) {
                const auto parts = parser.value("origin").split(',');
                QJsonObject origin;
                const char *keys[3]{"easting", "northing", "elevation"};
                if (parts.size() < 2 || parts.size() > 3)
                    invalid("--origin is E,N or E,N,Z in metres");
                for (qsizetype i = 0; i < parts.size(); ++i) {
                    bool ok{};
                    const double value = parts[i].trimmed().toDouble(&ok);
                    if (!ok)
                        invalid("--origin is E,N or E,N,Z in metres");
                    origin[keys[i]] = value;
                }
                command["origin"] = origin;
            }
            if (parser.isSet("terrain-crs"))
                command["crs"] = parser.value("terrain-crs");
            if (parser.isSet("terrain-columns")) {
                const auto value = parser.value("terrain-columns");
                if (value.contains(',')) {
                    QJsonArray columns;
                    for (const auto &column : value.split(','))
                        columns.append(column.trimmed());
                    command["columns"] = columns;
                } else
                    command["columns"] = value;
            }
            if (parser.isSet("terrain-delimiter"))
                command["delimiter"] = parser.value("terrain-delimiter");
            if (parser.isSet("terrain-header")) {
                const auto value = parser.value("terrain-header");
                if (value != "yes" && value != "no")
                    invalid("--terrain-header is yes or no");
                command["header"] = value == "yes";
            }
            if (parser.isSet("terrain-roles")) {
                QJsonObject roles;
                for (const auto &entry : parser.value("terrain-roles").split(',')) {
                    const auto at = entry.lastIndexOf('=');
                    if (at <= 0)
                        invalid("--terrain-roles is NAME=ROLE[,NAME=ROLE...]");
                    roles[entry.left(at)] = entry.mid(at + 1).trimmed();
                }
                command["roles"] = roles;
            }
            if (parser.isSet("terrain-name"))
                command["name"] = parser.value("terrain-name");
            command["sourceName"] = QFileInfo(path).fileName().left(256);
            QFile file(path);
            const QFileInfo info(path);
            if (!info.isFile() || info.size() > 64 * 1024 * 1024 ||
                !file.open(QIODevice::ReadOnly))
                throw sketchy::InspectionError("INPUT_ERROR",
                                               "A terrain source is a regular file of at most 64 MiB");
            const auto bytes = file.read(64 * 1024 * 1024 + 1);
            if (file.error() != QFileDevice::NoError || bytes.size() > 64 * 1024 * 1024)
                throw sketchy::InspectionError("INPUT_ERROR", "Cannot read the terrain source");
            auto document = parser.isSet("input") ? sketchy::loadDocument(parser.value("input"))
                                                  : sketchy::Document();
            // D-044: stage progress on stderr, only when stderr is a terminal, so scripts and
            // pipes see exactly the output they did before; stdout keeps the one JSON result.
            const bool terminal = ::isatty(STDERR_FILENO) == 1;
            int shown = -1;
            sketchy::TerrainProgress progress({}, [&](sketchy::TerrainStage stage, double fraction) {
                static const char *labels[]{"reading the source", "preparing the sources",
                                            "triangulating", "tiling"};
                const int percent =
                    int(std::floor(100 * sketchy::terrainOverallProgress(stage, fraction)));
                if (!terminal || percent == shown)
                    return;
                shown = percent;
                std::fprintf(stderr, "\r\x1b[KImporting terrain: %s, %d%%",
                             labels[int(stage)], percent);
                std::fflush(stderr);
            });
            // The progress line is cleared before anything else is printed.
            struct EndLine {
                const int &shown;
                ~EndLine() {
                    if (shown >= 0) {
                        std::fputs("\r\x1b[K", stderr);
                        std::fflush(stderr);
                    }
                }
            };
            QJsonObject operation;
            try {
                const EndLine endLine{shown};
                operation = sketchy::importTerrainSource(document, bytes, command, &progress);
            } catch (const sketchy::TerrainImportError &error) {
                throw sketchy::InspectionError(error.code(), error.what(),
                                               QJsonObject{{"importReport", error.report()}});
            } catch (const sketchy::TerrainError &error) {
                throw sketchy::InspectionError(error.code(), error.what());
            }
            QJsonObject result{{"status", "imported"},
                               {"terrain", operation["terrain"]},
                               {"tiles", operation["tiles"].toArray().size()},
                               {"siteEstablished", operation["siteEstablished"]},
                               {"importReport", operation["importReport"]}};
            if (parser.isSet("output"))
                result["nativeFile"] = sketchy::createNativeFile(document, parser.value("output"));
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        for (const auto &option : parser.optionNames())
            if (option.startsWith("terrain-") || option == "origin")
                throw sketchy::InspectionError(
                    "INVALID_REQUEST", "Terrain options require --import-terrain");
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
                          : QStringList{"export-dxf", "input", "dxf-unit", "dxf-site-coordinates"};
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
                const bool site = parser.isSet("dxf-site-coordinates");
                if (site && !document.site())
                    invalid("--dxf-site-coordinates needs a model with a site origin; this model "
                            "has none");
                const auto exported = sketchy::exportDxf(document, *options.metresPerUnit, site);
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
        if (parser.isSet("export-step")) {
            auto invalid = [](const std::string &message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            if (!parser.positionalArguments().isEmpty())
                invalid("STEP export requires named paths");
            const QStringList allowed{"export-step", "input", "step-schema", "step-deadline", "step-worker"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("STEP export is a standalone operation");
            if (!parser.isSet("input"))
                invalid("STEP export requires a native --input");
            sketchy::StepExportOptions options;
            if (parser.isSet("step-schema")) {
                const QString schema = parser.value("step-schema").toLower();
                if (schema == "ap242")
                    invalid("AP242 writing is not supported; use ap214 (the default) or ap203");
                if (schema != "ap214" && schema != "ap203")
                    invalid("--step-schema is ap214 or ap203");
                options.schema = schema;
            }
            sketchy::IfcWorkerOptions worker;
            if (parser.isSet("step-deadline")) {
                bool ok{};
                worker.deadlineSeconds = parser.value("step-deadline").toInt(&ok);
                if (!ok || worker.deadlineSeconds < 1 || worker.deadlineSeconds > 3600)
                    invalid("--step-deadline is 1 to 3600 seconds");
            }
            if (parser.isSet("step-worker"))
                worker.workerPath = parser.value("step-worker");
            const auto document = sketchy::loadDocument(parser.value("input"));
            QJsonObject result;
            try {
                const auto exported = sketchy::exportStepFile(document, parser.value("export-step"), options, worker);
                result = {{"status", "exported"}, {"exportReport", exported.report}};
            } catch (const sketchy::StepExportError &error) {
                const std::string code = error.code();
                const bool limit = code == "packageTooLarge" || code == "meshTooLarge" || code == "tooManyTriangles" || code == "outputTooLarge";
                const bool request = code == "destinationExists" || code == "destination" || code == "emptyModel" || code == "schema";
                throw sketchy::InspectionError(limit ? "LIMIT_EXCEEDED" : request ? "INVALID_REQUEST" : "INPUT_ERROR", error.what(),
                                               QJsonObject{{"stepCode", QString::fromStdString(code)},
                                                           {"exportReport", error.report()}});
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        if (parser.isSet("export-ifc")) {
            auto invalid = [](const std::string &message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            if (!parser.positionalArguments().isEmpty())
                invalid("IFC export requires named paths");
            const QStringList allowed{"export-ifc", "input", "ifc-timestamp", "ifc-deadline", "ifc-worker"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("IFC export is a standalone operation");
            if (!parser.isSet("input"))
                invalid("IFC export requires a native --input");
            sketchy::IfcExportOptions options;
            if (parser.isSet("ifc-timestamp")) {
                const auto text = parser.value("ifc-timestamp").trimmed();
                bool ok{};
                qint64 epoch = text.toLongLong(&ok);
                if (!ok) {
                    // YYYY-MM-DDTHH:MM:SS (optionally ending in Z), read as UTC: a zone with daylight saving has no say.
                    auto clock = text.endsWith('Z') ? text.chopped(1) : text;
                    const auto parts = clock.split('T');
                    const auto date = parts.size() == 2 ? QDate::fromString(parts[0], "yyyy-MM-dd") : QDate();
                    const auto time = parts.size() == 2 ? QTime::fromString(parts[1], "HH:mm:ss") : QTime();
                    if (!date.isValid() || !time.isValid() || parts[0].size() != 10 || parts[1].size() != 8)
                        invalid("--ifc-timestamp is UTC seconds since the epoch or YYYY-MM-DDTHH:MM:SS");
                    epoch = QDateTime(date, time, QTimeZone::utc()).toSecsSinceEpoch();
                }
                if (epoch < 0 || epoch > 253402300799LL)
                    invalid("--ifc-timestamp is between 1970 and 9999");
                options.timestamp = epoch;
            }
            sketchy::IfcWorkerOptions worker;
            if (parser.isSet("ifc-deadline")) {
                bool ok{};
                worker.deadlineSeconds = parser.value("ifc-deadline").toInt(&ok);
                if (!ok || worker.deadlineSeconds < 1 || worker.deadlineSeconds > 3600)
                    invalid("--ifc-deadline is 1 to 3600 seconds");
            }
            if (parser.isSet("ifc-worker"))
                worker.workerPath = parser.value("ifc-worker");
            const auto document = sketchy::loadDocument(parser.value("input"));
            QJsonObject result;
            try {
                const auto exported = sketchy::exportIfcFile(document, parser.value("export-ifc"), options, worker);
                result = {{"status", "exported"}, {"exportReport", exported.report}};
            } catch (const sketchy::IfcExportError &error) {
                const std::string code = error.code();
                const bool limit = code == "packageTooLarge" || code == "documentTooLarge" || code == "outputTooLarge";
                const bool request = code == "destinationExists" || code == "destination" || code == "badTimestamp" || code == "emptyModel";
                throw sketchy::InspectionError(limit ? "LIMIT_EXCEEDED" : request ? "INVALID_REQUEST" : "INPUT_ERROR", error.what(),
                                               QJsonObject{{"ifcCode", QString::fromStdString(code)},
                                                           {"exportReport", error.report()}});
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        if (parser.isSet("import-ifc")) {
            auto invalid = [](const std::string &message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            if (!parser.positionalArguments().isEmpty())
                invalid("IFC import requires named paths");
            const QStringList allowed{"import-ifc",  "output",     "ifc-deflection", "ifc-length-unit",
                                      "ifc-spaces",  "ifc-georef", "ifc-deadline", "ifc-worker"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("IFC import is a standalone operation");
            sketchy::IfcImportOptions options;
            const auto spaces = parser.isSet("ifc-spaces") ? parser.value("ifc-spaces") : QString("hidden");
            if (spaces == "hidden")
                options.spaces = sketchy::IfcSpaces::Hidden;
            else if (spaces == "shown")
                options.spaces = sketchy::IfcSpaces::Shown;
            else if (spaces == "skip")
                options.spaces = sketchy::IfcSpaces::Skip;
            else
                invalid("--ifc-spaces is hidden, shown or skip");
            const auto georef = parser.isSet("ifc-georef") ? parser.value("ifc-georef") : QString("keep");
            if (georef == "keep")
                options.georeference = sketchy::IfcGeoreference::KeepAxes;
            else if (georef == "grid")
                options.georeference = sketchy::IfcGeoreference::Grid;
            else
                invalid("--ifc-georef is keep or grid");
            sketchy::IfcWorkerOptions worker;
            if (parser.isSet("ifc-deflection")) {
                bool ok{};
                worker.chordDeviationMm = parser.value("ifc-deflection").toDouble(&ok);
                if (!ok || !(worker.chordDeviationMm >= 0.1 && worker.chordDeviationMm <= 1000))
                    invalid("--ifc-deflection is 0.1 to 1000 millimetres");
            }
            if (parser.isSet("ifc-length-unit")) {
                worker.lengthUnit = parser.value("ifc-length-unit");
                if (!QStringList{"mm", "cm", "m", "in", "ft"}.contains(worker.lengthUnit))
                    invalid("--ifc-length-unit is mm, cm, m, in or ft");
            }
            if (parser.isSet("ifc-deadline")) {
                bool ok{};
                worker.deadlineSeconds = parser.value("ifc-deadline").toInt(&ok);
                if (!ok || worker.deadlineSeconds < 1 || worker.deadlineSeconds > 3600)
                    invalid("--ifc-deadline is 1 to 3600 seconds");
            }
            if (parser.isSet("ifc-worker"))
                worker.workerPath = parser.value("ifc-worker");
            QJsonObject result;
            try {
                const auto imported = sketchy::importIfcFile(parser.value("import-ifc"), options, worker);
                result = {{"status", "imported"}, {"importReport", imported.report}};
                if (parser.isSet("output"))
                    result["nativeFile"] =
                        sketchy::createNativeFile(imported.document, parser.value("output"));
            } catch (const sketchy::IfcImportError &error) {
                const std::string code = error.code();
                const bool limit = code == "tooLarge" || code == "entityLimit" || code == "modelTooLarge" ||
                                   code == "farFromOrigin" || code == "documentLimit";
                throw sketchy::InspectionError(limit ? "LIMIT_EXCEEDED" : "INPUT_ERROR", error.what(),
                                               QJsonObject{{"ifcCode", QString::fromStdString(code)},
                                                           {"importReport", error.report()}});
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        // R098.a3 (ADR 0172 section 8): the STEP import, the IFC import's own operation with the
        // STEP's own names. The conversion is the IFC package's; the STEP's own options are the
        // tessellation's policy (section 3) and the unit for a file that declares none (section 2).
        if (parser.isSet("import-step")) {
            auto invalid = [](const std::string &message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            if (!parser.positionalArguments().isEmpty())
                invalid("STEP import requires named paths");
            const QStringList allowed{"import-step",  "output",     "step-deflection-mm", "step-angle-deg",
                                      "step-length-unit", "step-deadline", "step-worker"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("STEP import is a standalone operation");
            sketchy::StepWorkerOptions worker;
            if (parser.isSet("step-deflection-mm")) {
                bool ok{};
                worker.deflectionMm = parser.value("step-deflection-mm").toDouble(&ok);
                if (!ok || !(worker.deflectionMm >= 0.1 && worker.deflectionMm <= 1000))
                    invalid("--step-deflection-mm is 0.1 to 1000 millimetres");
            }
            if (parser.isSet("step-angle-deg")) {
                bool ok{};
                worker.angleDeg = parser.value("step-angle-deg").toDouble(&ok);
                if (!ok || !(worker.angleDeg > 0 && worker.angleDeg <= 180))
                    invalid("--step-angle-deg is 0 to 180 degrees");
            }
            if (parser.isSet("step-length-unit")) {
                worker.lengthUnitOverride = parser.value("step-length-unit");
                if (!QStringList{"mm", "cm", "m", "in", "ft"}.contains(worker.lengthUnitOverride))
                    invalid("--step-length-unit is mm, cm, m, in or ft");
            }
            if (parser.isSet("step-deadline")) {
                bool ok{};
                worker.deadlineSeconds = parser.value("step-deadline").toInt(&ok);
                if (!ok || worker.deadlineSeconds < 1 || worker.deadlineSeconds > 3600)
                    invalid("--step-deadline is 1 to 3600 seconds");
            }
            if (parser.isSet("step-worker"))
                worker.workerPath = parser.value("step-worker");
            QJsonObject result;
            try {
                const auto imported = sketchy::importStepFile(parser.value("import-step"), worker);
                result = {{"status", "imported"}, {"importReport", imported.report}};
                if (parser.isSet("output"))
                    result["nativeFile"] =
                        sketchy::createNativeFile(imported.document, parser.value("output"));
            } catch (const sketchy::IfcImportError &error) {
                const std::string code = error.code();
                const bool limit = code == "tooLarge" || code == "size" || code == "packageTooLarge" ||
                                   code == "modelTooLarge" || code == "documentLimit" ||
                                   code == "farFromOrigin";
                throw sketchy::InspectionError(limit ? "LIMIT_EXCEEDED" : "INPUT_ERROR", error.what(),
                                               QJsonObject{{"stepCode", QString::fromStdString(code)},
                                                           {"importReport", error.report()}});
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        // R102.c (ADR 0176 section 9): the SketchUp (.skp) import, the STEP import's operation with the
        // SketchUp helper's own options (the deadline and the path). The conversion is the package's
        // (R102.b), so the import has no options of its own.
        if (parser.isSet("import-skp")) {
            auto invalid = [](const std::string &message) {
                throw sketchy::InspectionError("INVALID_REQUEST", message);
            };
            if (!parser.positionalArguments().isEmpty())
                invalid("SketchUp import requires named paths");
            const QStringList allowed{"import-skp", "output", "skp-deadline", "skp-worker"};
            for (const auto &option : parser.optionNames())
                if (!allowed.contains(option))
                    invalid("SketchUp import is a standalone operation");
            sketchy::SkpWorkerOptions worker;
            if (parser.isSet("skp-deadline")) {
                bool ok{};
                worker.deadlineSeconds = parser.value("skp-deadline").toInt(&ok);
                if (!ok || worker.deadlineSeconds < 1 || worker.deadlineSeconds > 3600)
                    invalid("--skp-deadline is 1 to 3600 seconds");
            }
            if (parser.isSet("skp-worker"))
                worker.workerPath = parser.value("skp-worker");
            QJsonObject result;
            try {
                const auto imported = sketchy::importSkpFile(parser.value("import-skp"), worker);
                result = {{"status", "imported"}, {"importReport", imported.report}};
                if (parser.isSet("output"))
                    result["nativeFile"] =
                        sketchy::createNativeFile(imported.document, parser.value("output"));
            } catch (const sketchy::IfcImportError &error) {
                const std::string code = error.code();
                const bool limit = code == "tooLarge" || code == "packageTooLarge" || code == "documentLimit";
                throw sketchy::InspectionError(limit ? "LIMIT_EXCEEDED" : "INPUT_ERROR", error.what(),
                                               QJsonObject{{"skpCode", QString::fromStdString(code)},
                                                           {"importReport", error.report()}});
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return 0;
        }
        for (const auto &option : parser.optionNames())
            if (option.startsWith("ifc-"))
                throw sketchy::InspectionError("INVALID_REQUEST", "IFC options require --import-ifc or --export-ifc");
        for (const auto &option : parser.optionNames())
            if (option.startsWith("step-"))
                throw sketchy::InspectionError("INVALID_REQUEST", "STEP options require --import-step");
        for (const auto &option : parser.optionNames())
            if (option.startsWith("skp-"))
                throw sketchy::InspectionError("INVALID_REQUEST", "SketchUp options require --import-skp");
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
        QJsonObject failure{{"status", "failed"},
                            {"code", QString::fromStdString(e.code())},
                            {"error", e.what()}};
        if (!e.details().isEmpty())
            failure["details"] = e.details();
        std::cerr << QJsonDocument(failure)
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
