// Guards the in-app assistant's provider-facing tool list (ADR 0030, ADR 0164).
//
// The tool list is re-sent on every provider turn, so its size is a per-turn input cost.
// Each surface below is compared byte-for-byte with a committed baseline
// (tests/assistant-tool-surface-baseline.json). It was dumped from the v1.0.1 build and
// re-pinned once, by R095.g, to add site.describe, terrain.describe and terrain.elevation_at
// (ADR 0168 decision 10). Any change to what the assistant sees therefore has to update that
// baseline deliberately.
//
//   assistant_tool_surface_tests            check against the baseline
//   assistant_tool_surface_tests --dump     print the current surfaces as a new baseline
//
// Before R095.g the source compiled unchanged against v1.0.1, which is how the first
// baseline was produced.
#include "automation/assistant.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/mcp.hpp"
#include "integrations/openai_provider.hpp"
#include "terrain_import_fixtures.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <cmath>
#include <iostream>
#include <optional>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray compact(const QJsonArray &value) {
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
struct ToolSurface {
    QString name;
    QStringList commands;
    bool clarification{};
};
// The corpus v2 task authorizations (tests/local_provider_trial.cpp) and a superset of the
// desktop assistant: every assistant-authorizable command plus assistant.ask_user.
std::vector<ToolSurface> surfaces() {
    QStringList every;
    for (const auto &entry : commandCatalog()) {
        const auto name = entry.toObject()["name"].toString();
        // Drawing-sheet, component parameter, site, terrain, camera path, render preset, style and
        // catalog commands are never assistant-authorizable (ADR 0164, ADR 0165 §6, ADR 0168 §5,
        // ADR 0173 §7, ADR 0174 decision 9 and section 7).
        if (!name.startsWith("drawing_") && !name.startsWith("component_parameter") &&
            !name.startsWith("site.") && !name.startsWith("terrain.") &&
            !name.startsWith("camera_path.") && !name.startsWith("render_preset.") &&
            !name.startsWith("style.") && !name.startsWith("catalog."))
            every.append(name);
    }
    return {{"inspection-only", {}, false},
            {"room", {"assembly.room"}, false},
            {"resize", {"assembly.window.resize"}, false},
            {"advanced",
             {"assembly.roof", "assembly.stairs", "assembly.table", "assembly.cabinet",
              "assert.measurement"},
             false},
            {"site", {"assembly.site_place", "assert.measurement"}, false},
            {"every-command-with-clarification", every, true}};
}
QJsonObject firstRequest(const ToolSurface &surface) {
    AssistantTask::Options options;
    options.prompt = "Tool surface fixture.";
    options.provider = "OpenAI";
    options.model = "tool-surface-fixture";
    options.allowedCommands = surface.commands;
    options.clarificationAvailable = surface.clarification;
    AssistantTask task(
        AssistantBackend{[](const QJsonObject &) { return QJsonObject{}; },
                         [] {
                             return QJsonObject{{"documentId", "tool-surface-document"},
                                                {"revision", "1"}};
                         }},
        options);
    const auto request = task.nextRequest();
    check(bool(request), "First provider request");
    return *request;
}
QJsonObject describe(const QJsonObject &request) {
    const auto tools = request["tools"].toArray();
    const auto bytes = compact(tools);
    // The ChatGPT-plan wire form the release corpus uses (R086.ca/R086.cb "serialized tools").
    const auto wire = compact(OpenAiConversation(true).request(request)["tools"].toArray());
    QJsonArray names;
    for (const auto &tool : tools)
        names.append(tool.toObject()["name"]);
    return {{"toolCount", tools.size()},
            {"toolsBytes", bytes.size()},
            {"toolsSha256",
             QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"chatGptPlanToolsBytes", wire.size()},
            {"names", names}};
}
// Every operation/query/command discriminator reachable anywhere in a schema.
void discriminators(const QJsonValue &value, QStringList &out) {
    if (value.isArray())
        for (const auto &item : value.toArray())
            discriminators(item, out);
    if (!value.isObject())
        return;
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() == "name" && it.value().isString())
            out.append(it.value().toString());
        if ((it.key() == "operation" || it.key() == "query" || it.key() == "command") &&
            it.value().toObject().contains("const"))
            out.append(it.value().toObject()["const"].toString());
        discriminators(it.value(), out);
    }
}
bool isDrawing(const QString &name) { return name.startsWith("drawing_"); }
// ADR 0168 decision 10: the only site and terrain names the assistant sees.
const QStringList terrainVisible{"site.describe", "terrain.describe", "terrain.elevation_at"};
bool isSiteOrTerrain(const QString &name) {
    return name.startsWith("site.") || name.startsWith("terrain.");
}
// ADR 0173 decision 13: the three presentation queries are the only camera path, render preset
// and presentation names the assistant sees; their commands stay hidden.
const QStringList presentationVisible{"camera_path.describe", "render_preset.describe",
                                      "presentation.describe"};
bool isPresentation(const QString &name) {
    return name.startsWith("camera_path.") || name.startsWith("render_preset.") ||
           name.startsWith("presentation.");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc == 2 && QByteArray(argv[1]) == "--dump") {
            QJsonObject result;
            for (const auto &surface : surfaces())
                result[surface.name] = describe(firstRequest(surface));
            std::cout << QJsonDocument(result).toJson().constData();
            return 0;
        }
        // R099.c: write each surface's first provider request (the ChatGPT-plan wire form the
        // corpus uses) to <dir>/<surface>.json, for the offline token measurement in the ADR notes.
        if (argc == 3 && QByteArray(argv[1]) == "--requests") {
            for (const auto &surface : surfaces()) {
                QFile file(QString(argv[2]) + "/" + surface.name + ".json");
                check(file.open(QIODevice::WriteOnly), "Open request dump");
                file.write(QJsonDocument(OpenAiConversation(true).request(firstRequest(surface)))
                               .toJson(QJsonDocument::Compact));
            }
            return 0;
        }
        QFile file(SOURCE_DIR "/tests/assistant-tool-surface-baseline.json");
        check(file.open(QIODevice::ReadOnly), "Read the assistant tool-surface baseline");
        const auto baseline = QJsonDocument::fromJson(file.readAll()).object();
        const auto expected = baseline["surfaces"].toObject();
        check(expected.size() == int(surfaces().size()), "Baseline covers every surface");
        for (const auto &surface : surfaces()) {
            const auto name = surface.name.toStdString();
            const auto request = firstRequest(surface);
            // 1. No drawing-sheet operation, query or command anywhere in the provider schemas,
            // including transaction.inspect requests and transaction.apply command lists.
            QStringList found;
            discriminators(request["tools"], found);
            check(!found.isEmpty(), name + ": discriminators found");
            for (const auto &entry : found)
                check(!isDrawing(entry) &&
                          (!isSiteOrTerrain(entry) || terrainVisible.contains(entry)) &&
                          (!isPresentation(entry) || presentationVisible.contains(entry)),
                      name + ": assistant sees " + entry.toStdString());
            // The three decision-10 queries are top-level tools on every surface.
            QStringList top;
            for (const auto &tool : request["tools"].toArray())
                top.append(tool.toObject()["name"].toString());
            for (const auto &query : terrainVisible)
                check(top.contains(query), name + ": assistant lacks " + query.toStdString());
            for (const auto &query : presentationVisible)
                check(top.contains(query), name + ": assistant lacks " + query.toStdString());
            // ADR 0175 decision 12 (R101.d): the report query and the report schedule operation
            // stay off the provider surface on every surface, so the baseline is unchanged.
            for (const auto *report : {"report.run", "report.schedule"})
                check(!top.contains(report), name + ": assistant sees " + report);
            // R100.b2 (ADR 0174 decision 9): the material texture size stays off every assistant
            // schema, so the pinned bytes below do not move when the material commands gain it.
            check(!QJsonDocument(request["tools"].toArray())
                       .toJson(QJsonDocument::Compact)
                       .contains("textureSize"),
                  name + ": assistant sees the material texture size");
            // 2. The surface equals the baseline byte for byte.
            const auto actual = describe(request);
            const auto want = expected[surface.name].toObject();
            for (const auto *key : {"toolCount", "toolsBytes", "toolsSha256",
                                    "chatGptPlanToolsBytes", "names"})
                check(actual[key] == want[key],
                      name + ": " + key + " differs from the baseline: " +
                          QJsonDocument(QJsonObject{{"actual", actual[key]}})
                              .toJson(QJsonDocument::Compact)
                              .toStdString());
        }
        // 3. Drawing commands cannot be authorized for, or called by, an assistant task.
        {
            ToolSurface sheet{"sheet", {"drawing_sheet.create"}, false};
            try {
                firstRequest(sheet);
                check(false, "Drawing command authorization must be rejected");
            } catch (const InspectionError &error) {
                check(error.code() == "UNSUPPORTED_CAPABILITY", error.what());
            }
            AssistantTask::Options options;
            options.prompt = "List the drawing sheets.";
            options.provider = "fixture";
            options.model = "fixture";
            int backendCalls{};
            AssistantTask task(AssistantBackend{[&](const QJsonObject &) {
                                                    ++backendCalls;
                                                    return QJsonObject{};
                                                },
                                                [] {
                                                    return QJsonObject{{"documentId", "doc"},
                                                                       {"revision", "1"}};
                                                }},
                               options);
            const auto request = task.nextRequest();
            check(bool(request), "Inspection-only request");
            AssistantReply reply;
            reply.calls.push_back({"1", "drawing_sheets.query",
                                   QJsonObject{{"apiVersion", 1},
                                               {"documentId", "doc"},
                                               {"expectedRevision", "1"},
                                               {"query", "drawing_sheets.query"}}});
            reply.inputTokens = 10;
            reply.outputTokens = 5;
            check(task.accept(request->value("attemptId").toString(), reply), "Reply accepted");
            const auto next = task.nextRequest();
            check(bool(next), "Tool error is returned to the provider");
            const auto message = next->value("messages").toArray().last().toObject();
            check(message["isError"].toBool() &&
                      QJsonDocument(message).toJson().contains("UNSUPPORTED_CAPABILITY") &&
                      backendCalls == 0,
                  "A drawing query call is outside the assistant's authorization");
        }
        // 4. MCP keeps every drawing-sheet tool, so external agents still drive sheets.
        {
            QStringList names;
            QStringList nested;
            for (const auto &tool : mcpToolCatalog()) {
                names.append(tool.toObject()["name"].toString());
                discriminators(tool.toObject()["inputSchema"], nested);
            }
            for (const auto *query :
                 {"drawing_sheets.query", "drawing_sheet.describe", "drawing_views.status",
                  "drawing_view.describe", "drawing_items.query", "drawing_item.describe",
                  "drawing_item.reattach_candidates", "drawing_sheets.export_preflight"})
                check(names.contains(query), std::string("MCP lists ") + query);
            for (const auto *command : {"drawing_sheet.create", "drawing_view.create",
                                        "drawing_item.create", "drawing_view.accept"})
                check(nested.contains(command),
                      std::string("MCP transaction.apply accepts ") + command);
            // MCP exposes every site and terrain query and command (ADR 0168 decision 10).
            for (const auto *query : {"site.describe", "site.convert", "terrain.describe",
                                      "terrain.elevation_at"})
                check(names.contains(query), std::string("MCP lists ") + query);
            for (const auto *command :
                 {"site.set", "site.reanchor", "terrain.import", "terrain.inspect_source",
                  "terrain.create_from_edges", "terrain.set_parameters", "terrain.regenerate",
                  "terrain.set_display", "terrain.explode"})
                check(nested.contains(command),
                      std::string("MCP transaction.apply accepts ") + command);
        }
        // R100.b2: MCP's material commands take the texture size.
        {
            int sized = 0;
            for (const auto &tool : mcpToolCatalog()) {
                const auto entry = tool.toObject();
                const auto toolName = entry["name"].toString();
                if (toolName == "material.create" || toolName == "material.edit")
                    sized += QJsonDocument(entry["inputSchema"].toObject())
                                 .toJson(QJsonDocument::Compact)
                                 .contains("textureSize");
            }
            check(sized == 2, "MCP material.create and material.edit accept the texture size");
        }
        // 5. Site and terrain commands cannot be authorized for an assistant task, and the
        // hidden query site.convert is rejected without reaching the document.
        {
            for (const auto *command :
                 {"site.set", "site.reanchor", "terrain.import", "terrain.inspect_source",
                  "terrain.create_from_edges", "terrain.set_parameters", "terrain.regenerate",
                  "terrain.set_display", "terrain.explode"}) {
                try {
                    firstRequest({"terrain", {command}, false});
                    check(false, std::string("Authorization must be rejected: ") + command);
                } catch (const InspectionError &error) {
                    check(error.code() == "UNSUPPORTED_CAPABILITY", error.what());
                }
            }
            AssistantTask::Options options;
            options.prompt = "Convert a point to site coordinates.";
            options.provider = "fixture";
            options.model = "fixture";
            int backendCalls{};
            AssistantTask task(AssistantBackend{[&](const QJsonObject &) {
                                                    ++backendCalls;
                                                    return QJsonObject{};
                                                },
                                                [] {
                                                    return QJsonObject{{"documentId", "doc"},
                                                                       {"revision", "1"}};
                                                }},
                               options);
            const auto request = task.nextRequest();
            check(bool(request), "Inspection-only request");
            AssistantReply reply;
            reply.calls.push_back({"1", "site.convert",
                                   QJsonObject{{"apiVersion", 1},
                                               {"documentId", "doc"},
                                               {"expectedRevision", "1"},
                                               {"query", "site.convert"},
                                               {"to", "site"},
                                               {"points", QJsonArray{QJsonArray{0, 0, 0}}}}});
            reply.inputTokens = 10;
            reply.outputTokens = 5;
            check(task.accept(request->value("attemptId").toString(), reply), "Reply accepted");
            const auto next = task.nextRequest();
            check(bool(next), "Tool error is returned to the provider");
            const auto message = next->value("messages").toArray().last().toObject();
            check(message["isError"].toBool() &&
                      QJsonDocument(message).toJson().contains("UNSUPPORTED_CAPABILITY") &&
                      backendCalls == 0,
                  "site.convert is outside the assistant's authorization");
        }
        // 6. The assistant calls the three queries on a document with an imported terrain
        // (the R095.c UTM fixture, which also establishes the site origin).
        {
            Document doc;
            executeBatch(
                doc, QJsonObject{
                         {"apiVersion", 1},
                         {"documentId", QString::fromStdString(doc.identity())},
                         {"expectedRevision", QString::number(doc.revision())},
                         {"commands",
                          QJsonArray{QJsonObject{
                              {"command", "terrain.import"},
                              {"data", QString::fromLatin1(terrain_fixture::terrainUtm().toBase64())},
                              {"format", "dxf"},
                              {"crs", "EPSG:32633"},
                              {"roles", QJsonObject{{"BOUNDARY", "boundary"}, {"POND", "hole"}}}}}}});
            check(doc.site().has_value(), "The fixture establishes a site origin");
            Id terrain{};
            for (const auto &[id, body] : doc.bodies())
                if (body->kind == BodyKind::Terrain)
                    terrain = id;
            check(terrain != 0, "The fixture imports one terrain");
            // A tile vertex in world (model) coordinates: elevation_at returns its Z exactly.
            std::optional<Vec3> vertex;
            for (const auto &[id, body] : doc.bodies())
                if (!vertex && body->parent == terrain && !body->surface.vertices.empty())
                    vertex = doc.worldTransform(id).point(body->surface.vertices.begin()->second);
            check(bool(vertex), "The terrain has tiles");
            const auto documentId = QString::fromStdString(doc.identity());
            const auto revision = QString::number(doc.revision());
            AssistantTask::Options options;
            options.prompt = "How high is the ground at this point?";
            options.provider = "fixture";
            options.model = "fixture";
            AssistantTask task(
                AssistantBackend{[&](const QJsonObject &request) {
                                     return inspectDocument(doc, request);
                                 },
                                 [&] {
                                     return QJsonObject{{"documentId", documentId},
                                                        {"revision", revision}};
                                 }},
                options);
            auto pending = task.nextRequest();
            check(bool(pending), "Terrain inspection request");
            int count{};
            auto call = [&](const QString &name, QJsonObject args) {
                args["apiVersion"] = 1;
                args["documentId"] = documentId;
                args["expectedRevision"] = revision;
                args["query"] = name;
                AssistantReply reply;
                reply.calls.push_back({QString::number(++count), name, args});
                reply.inputTokens = 10;
                reply.outputTokens = 5;
                check(task.accept(pending->value("attemptId").toString(), reply),
                      "Terrain query reply accepted");
                pending = task.nextRequest();
                check(bool(pending), "Next provider turn");
                const auto message = pending->value("messages").toArray().last().toObject();
                check(!message["isError"].toBool(), QJsonDocument(message).toJson().constData());
                // The inspection envelope carries the query's own result in "data".
                return message["data"].toObject()["data"].toObject();
            };
            const auto site = call("site.describe", {});
            check(site["established"] == true && site["units"] == "m" &&
                      site["origin"].toObject()["easting"].toDouble() == doc.site()->easting &&
                      site["crs"] == "EPSG:32633",
                  "site.describe returns the origin in metres");
            const auto described = call("terrain.describe", {{"body", QString::number(terrain)}});
            check(described["body"] == QString::number(terrain) &&
                      described["triangles"].toDouble() > 0 && described["units"] == "m" &&
                      described["elevationRange"].toArray().size() == 2,
                  "terrain.describe returns the terrain");
            const auto heights = call(
                "terrain.elevation_at",
                {{"body", QString::number(terrain)},
                 {"points", QJsonArray{QJsonArray{vertex->x, vertex->y}, QJsonArray{9e5, 9e5}}}});
            const auto points = heights["points"].toArray();
            check(heights["space"] == "model" && heights["units"] == "m" && points.size() == 2,
                  "terrain.elevation_at answers in model metres");
            check(points[0].toObject()["found"] == true &&
                      std::abs(points[0].toObject()["elevation"].toDouble() - vertex->z) <= 1e-9,
                  "elevation_at returns a tile vertex's elevation");
            check(points[1].toObject()["found"] == false &&
                      points[1].toObject()["elevation"].isNull(),
                  "Off the terrain, found is false and elevation is null");
            check(doc.revision() == revision.toULongLong(), "The queries change nothing");
        }
        std::cout << "The assistant's provider tool list has no drawing-sheet tools, shows only "
                     "the decision-10 site and terrain queries and the ADR 0173 presentation queries, and equals the baseline; "
                     "MCP still exposes every tool\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
