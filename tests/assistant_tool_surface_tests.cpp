// Guards the in-app assistant's provider-facing tool list (ADR 0030, ADR 0164).
//
// The tool list is re-sent on every provider turn, so its size is a per-turn input cost.
// Each surface below is compared byte-for-byte with a committed baseline dumped from the
// v1.0.1 build (tests/assistant-tool-surface-baseline.json). Any change to what the assistant
// sees therefore has to update that baseline deliberately.
//
//   assistant_tool_surface_tests            check against the baseline
//   assistant_tool_surface_tests --dump     print the current surfaces as a new baseline
//
// The source compiles unchanged against v1.0.1, which is how the baseline was produced.
#include "automation/assistant.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/mcp.hpp"
#include "integrations/openai_provider.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
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
        if (!name.startsWith("drawing_"))
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
                check(!isDrawing(entry), name + ": assistant sees " + entry.toStdString());
            // 2. The surface equals the 1.0.1 surface byte for byte.
            const auto actual = describe(request);
            const auto want = expected[surface.name].toObject();
            for (const auto *key : {"toolCount", "toolsBytes", "toolsSha256",
                                    "chatGptPlanToolsBytes", "names"})
                check(actual[key] == want[key],
                      name + ": " + key + " differs from the 1.0.1 baseline: " +
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
        }
        std::cout << "The assistant's provider tool list has no drawing-sheet tools and equals "
                     "the 1.0.1 surface; MCP still exposes them\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
