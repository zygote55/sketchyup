// R093.d: component parameter commands, inspection queries, MCP/headless exposure and
// the assistant surface (ADR 0165 §4, §6).
#include "automation/assistant.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/mcp.hpp"
#include "automation/session.hpp"
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
#include <iostream>
using namespace sketchy;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
QString text(Id id) { return QString::number(id); }
std::string json(const QJsonValue &value) {
    return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).toStdString();
}
QJsonObject request(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject run(Document &doc, QJsonArray commands) {
    return executeBatch(doc, request(doc, std::move(commands)));
}
QJsonObject query(const Document &doc, const char *name, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return inspectDocument(doc, extra)["data"].toObject();
}
// The error code a refusal carries ("" for a plain runtime error), with its message.
std::pair<std::string, std::string> refusal(const std::function<void()> &operation,
                                            const std::string &label) {
    try {
        operation();
    } catch (const InspectionError &error) {
        return {error.code(), error.what()};
    } catch (const std::exception &error) {
        return {"", error.what()};
    }
    throw std::runtime_error(label + ": expected a refusal, but the operation succeeded");
}
// Every model record table by pointer, the allocators, revision, history and dirty
// state: a refused batch must leave all of them unchanged (ADR 0165 §4).
struct Records {
    std::map<Id, BodyPtr> bodies;
    ComponentDefinitions definitions;
    ComponentInstances instances;
    ComponentVariantsPtr variants;
    std::vector<Id> allocators;
    std::uint64_t revision{};
    std::size_t history{}, historyBytes{};
    bool dirty{};
    explicit Records(const Document &doc)
        : bodies(doc.bodies()), definitions(doc.definitions()), instances(doc.instances()),
          variants(doc.componentVariantRecords()),
          allocators{doc.nextId(), doc.nextDefinitionId(), doc.nextComponentDriverId()},
          revision(doc.revision()), history(doc.history().total),
          historyBytes(doc.historyBytes()), dirty(doc.dirty()) {}
    bool operator==(const Records &) const = default;
};
Id box(Document &doc, Vec3 low, Vec3 high, const std::string &name) {
    const auto id = doc.addFace({{{low.x, low.y, low.z},
                                  {high.x, low.y, low.z},
                                  {high.x, high.y, low.z},
                                  {low.x, high.y, low.z}}},
                                name);
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, high.z - low.z);
    return id;
}
double maxX(const Body &body) {
    double result = -1e9;
    for (const auto &[id, point] : body.surface.vertices)
        result = std::max(result, point.x);
    return result;
}
QJsonObject define(Id definition, const char *name, const char *label, const char *type,
                   const char *mode, const char *expression, QJsonObject extra = {}) {
    extra["command"] = "component_parameter.define";
    extra["definition"] = text(definition);
    extra["name"] = name;
    extra["label"] = label;
    extra["type"] = type;
    extra["mode"] = mode;
    extra["expression"] = expression;
    return extra;
}
QJsonObject drive(Id definition, const char *kind, Id member, const char *expression,
                  QJsonObject data, Id driver = 0) {
    QJsonObject command{{"command", "component_parameter.drive"},
                        {"definition", text(definition)},
                        {"kind", kind},
                        {"member", text(member)},
                        {"expression", expression},
                        {"data", data}};
    if (driver)
        command["driver"] = text(driver);
    return command;
}
QJsonObject override(Id placement, QJsonObject values) {
    return {{"command", "component_parameter.override"},
            {"body", text(placement)},
            {"values", values}};
}
// The table of ADR 0165 §1, made raw and grouped; the component and its parameters
// are created by the command API.
struct Table {
    Document doc;
    Id group{}, top{}, legRightFront{}, legRightBack{}, shelf{};
    Id definition{}, placement{};
    Id stretch{}, legFront{}, legBack{}, repeat{}, visibility{};
};
Table rawTable() {
    Table t;
    auto &doc = t.doc;
    t.top = box(doc, {0, 0, .72}, {.9, .6, .75}, "Top");
    const auto leftFront = box(doc, {0, 0, 0}, {.05, .05, .72}, "Leg left front");
    const auto leftBack = box(doc, {0, .55, 0}, {.05, .6, .72}, "Leg left back");
    t.legRightFront = box(doc, {.84, 0, 0}, {.89, .05, .72}, "Leg right front");
    t.legRightBack = box(doc, {.84, .55, 0}, {.89, .6, .72}, "Leg right back");
    t.shelf = box(doc, {.05, .05, .2}, {.84, .55, .22}, "Shelf");
    t.group = createGroup(
        doc, {t.top, leftFront, leftBack, t.legRightFront, t.legRightBack, t.shelf}, "Table");
    return t;
}
QJsonArray tableCommands(const Table &t, Id definition) {
    return {define(definition, "width", "Width", "length", "input", "900mm",
                   {{"min", .4}, {"max", 2.4}}),
            define(definition, "shelves", "Shelves", "count", "input", "1",
                   {{"min", 0}, {"max", 4}}),
            define(definition, "leg_x", "Leg position", "length", "derived", "width-60 mm"),
            drive(definition, "stretch", t.top, "width", {{"axis", "x"}, {"at", .45}}),
            drive(definition, "position", t.legRightFront, "leg_x", {{"axis", "x"}}),
            drive(definition, "position", t.legRightBack, "leg_x", {{"axis", "x"}}),
            drive(definition, "repeat", t.shelf, "max(shelves,1)",
                  {{"axis", "z"}, {"maxCount", 4}, {"spacing", "300 mm"}}),
            drive(definition, "visibility", t.shelf, "shelves > 0", {})};
}
// One batch makes the component, defines its parameters and drives its members
// (ADR 0165 §4 "One change, one entry").
Table makeTable() {
    auto t = rawTable();
    auto &doc = t.doc;
    const auto definition = doc.nextDefinitionId();
    auto commands = tableCommands(t, definition);
    commands.prepend(QJsonObject{{"command", "component.create"},
                                 {"body", text(t.group)},
                                 {"name", "Table"}});
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    const auto result = run(doc, commands);
    check(doc.revision() == revision + 1 && doc.history().total == history + 1,
          "Component creation, parameters and drivers commit as one entry and revision");
    t.definition = definition;
    t.placement = t.group;
    check(doc.instances().contains(t.placement) &&
              doc.instances().at(t.placement)->definition == definition,
          "The group became the placement of the new definition");
    const auto created = result["createdComponentDrivers"].toArray();
    check(created.size() == 5, "Five drivers are reported as created: " + json(created));
    t.stretch = created[0].toString().toULongLong();
    t.legFront = created[1].toString().toULongLong();
    t.legBack = created[2].toString().toULongLong();
    t.repeat = created[3].toString().toULongLong();
    t.visibility = created[4].toString().toULongLong();
    check(result["createdDefinitions"].toArray() == QJsonArray{QJsonValue(text(definition))},
          "The batch reports its definition beside its drivers");
    check(result["parameterWarnings"].toArray().isEmpty(), "A new definition has no warnings");
    const auto &record = *doc.definitions().at(definition)->parameters;
    check(record.parameters.size() == 3 && record.parameters[0].expression == "900 mm" &&
              record.parameters[2].expression == "width - 60 mm",
          "Expressions are stored as canonical text");
    return t;
}
void surface() {
    const QStringList commands{"component_parameter.define",  "component_parameter.update",
                               "component_parameter.rename",  "component_parameter.remove",
                               "component_parameter.drive",   "component_parameter.undrive",
                               "component_parameter.rebind",  "component_parameter.override",
                               "component_parameter.reset"};
    const QStringList queries{"component_parameters.describe", "component_parameters.values",
                              "component_parameters.evaluate"};
    QStringList catalogue;
    for (const auto &entry : commandCatalog())
        catalogue.append(entry.toObject()["name"].toString());
    for (const auto &name : commands)
        check(catalogue.contains(name), "The command catalogue lists " + name.toStdString());
    QStringList inspection;
    for (const auto &entry : inspectionCatalog())
        inspection.append(entry.toObject()["name"].toString());
    for (const auto &name : queries)
        check(inspection.contains(name), "The inspection catalogue lists " + name.toStdString());
    // MCP lists every query as a tool and accepts every operation in transaction.apply;
    // the headless session advertises the same.
    const auto mcp = QJsonDocument(mcpToolCatalog()).toJson(QJsonDocument::Compact);
    const auto session = QJsonDocument(sessionCapabilities()).toJson(QJsonDocument::Compact);
    QStringList tools;
    for (const auto &tool : mcpToolCatalog())
        tools.append(tool.toObject()["name"].toString());
    for (const auto &name : queries) {
        check(tools.contains(name), "MCP lists " + name.toStdString());
        check(session.contains(("\"" + name + "\"").toUtf8()),
              "The headless session lists " + name.toStdString());
    }
    for (const auto &name : commands) {
        check(mcp.contains(("{\"const\":\"" + name + "\"}").toUtf8()),
              "MCP transaction.apply accepts " + name.toStdString());
        check(session.contains(("{\"const\":\"" + name + "\"}").toUtf8()),
              "The headless session accepts " + name.toStdString());
    }
    // The assistant neither sees nor runs them (ADR 0165 §6).
    {
        AssistantTask::Options options;
        options.prompt = "Widen the table.";
        options.provider = "fixture";
        options.model = "fixture";
        options.allowedCommands = {"component_parameter.override"};
        const auto [code, message] = refusal(
            [&] {
                AssistantTask task(AssistantBackend{[](const QJsonObject &) { return QJsonObject{}; },
                                                    [] {
                                                        return QJsonObject{{"documentId", "doc"},
                                                                           {"revision", "1"}};
                                                    }},
                                   options);
            },
            "assistant authorization");
        check(code == "UNSUPPORTED_CAPABILITY",
              "A parameter command cannot be authorized for the assistant: " + message);
    }
    {
        AssistantTask::Options options;
        options.prompt = "Describe the table's parameters.";
        options.provider = "fixture";
        options.model = "fixture";
        int backendCalls = 0;
        AssistantTask task(AssistantBackend{[&](const QJsonObject &) {
                                                ++backendCalls;
                                                return QJsonObject{};
                                            },
                                            [] {
                                                return QJsonObject{{"documentId", "doc"},
                                                                   {"revision", "1"}};
                                            }},
                           options);
        const auto first = task.nextRequest();
        check(bool(first), "Inspection-only assistant request");
        const auto tools = QJsonDocument(first->value("tools").toArray()).toJson();
        check(!tools.contains("component_parameter"), "The provider tool list has no parameter tool");
        AssistantReply reply;
        reply.calls.push_back({"1", "component_parameters.describe",
                               QJsonObject{{"apiVersion", 1},
                                           {"documentId", "doc"},
                                           {"expectedRevision", "1"},
                                           {"query", "component_parameters.describe"},
                                           {"definition", "1"}}});
        reply.inputTokens = 10;
        reply.outputTokens = 5;
        check(task.accept(first->value("attemptId").toString(), reply), "Reply accepted");
        const auto next = task.nextRequest();
        check(bool(next), "The tool error returns to the provider");
        const auto message = next->value("messages").toArray().last().toObject();
        check(message["isError"].toBool() &&
                  QJsonDocument(message).toJson().contains("UNSUPPORTED_CAPABILITY") &&
                  backendCalls == 0,
              "An assistant parameter query is rejected as UNSUPPORTED_CAPABILITY");
    }
    std::cout << "surface: ok\n";
}
// A failing last operation refuses the whole batch and changes nothing.
void refused(Document &doc, QJsonArray commands, const std::string &code,
             const std::string &label) {
    const Records before(doc);
    const auto [actual, message] = refusal([&] { run(doc, commands); }, label);
    check(actual == code, label + ": expected " + code + ", got '" + actual + "': " + message);
    check(message.size() <= 2048, label + ": message is bounded");
    check(Records(doc) == before, label + ": every record is pointer-identical and unchanged");
}
void atomicBatches() {
    auto t = makeTable();
    auto &doc = t.doc;
    // Undo removes the component and its parameters together; redo restores them.
    const auto parametric = doc.definitions().at(t.definition);
    doc.undo();
    check(!doc.definitions().contains(t.definition) && !doc.instances().contains(t.placement),
          "Undo removes the definition with its parameters in one step");
    check(doc.nextComponentDriverId() > t.visibility, "The driver allocator never rewinds");
    doc.redo();
    check(*doc.definitions().at(t.definition)->parameters == *parametric->parameters,
          "Redo restores the exact parameter record");
    // A parameter batch on an existing component: define, drive and override together.
    const auto revision = doc.revision();
    const auto result = run(doc, {define(t.definition, "depth", "Depth", "length", "input",
                                         "600 mm"),
                                  drive(t.definition, "stretch", t.top, "depth",
                                        {{"axis", "y"}, {"at", .3}}),
                                  override(t.placement, {{"width", "1200 mm"}, {"depth", .7}})});
    check(doc.revision() == revision + 1, "Three parameter operations publish one revision");
    check(result["createdComponentDrivers"].toArray().size() == 1, "One driver created");
    const auto regenerated = result["regenerated"].toObject();
    check(regenerated["placements"].toInt() == 1 && regenerated["variants"].toInt() == 1,
          "The placement regenerated into one new variant: " + json(regenerated));
    check(result["componentOperations"].toArray().size() == 3,
          "Each parameter operation reports itself");
    const auto top = doc.instances().at(t.placement)->members.at(t.top);
    check(std::abs(maxX(*doc.bodies().at(top)) - 1.2) < 1e-9, "The top is 1.2 m long");
    doc.undo();
    check(doc.revision() == revision + 2 && doc.instances().at(t.placement)->overrides.empty() &&
              doc.definitions().at(t.definition)->parameters->parameters.size() == 3,
          "One undo reverts the whole parameter batch");
    // Stale revision: nothing runs.
    auto stale = request(doc, {override(t.placement, {{"width", 1.0}})});
    stale["expectedRevision"] = QString::number(doc.revision() - 1);
    const Records before(doc);
    const auto [code, message] = refusal([&] { executeBatch(doc, stale); }, "stale");
    check(message == "STALE_REVISION" && Records(doc) == before,
          "A stale batch is refused and changes nothing");
    // Overrides: SI numbers, quantity strings, booleans and choices.
    run(doc, {define(t.definition, "finish", "Finish", "choice", "input", "\"oak\"",
                     {{"options", QJsonArray{QJsonObject{{"value", "oak"}, {"label", "Oak"}},
                                             QJsonObject{{"value", "ash"}, {"label", "Ash"}}}}}),
              define(t.definition, "rails", "Rails", "boolean", "input", "false"),
              override(t.placement, {{"width", 1.1},
                                     {"shelves", 2},
                                     {"finish", "ash"},
                                     {"rails", true}})});
    const auto &overrides = doc.instances().at(t.placement)->overrides;
    check(overrides.size() == 4 &&
              std::get<ParameterQuantity>(overrides.at("width")).value ==
                  quantizeParameterLength(1.1) &&
              std::get<ParameterChoice>(overrides.at("finish")).value == "ash" &&
              std::get<bool>(overrides.at("rails")),
          "Override literals are canonicalized");
    run(doc, {QJsonObject{{"command", "component_parameter.reset"},
                          {"body", text(t.placement)},
                          {"names", QJsonArray{"rails", "finish"}}}});
    check(doc.instances().at(t.placement)->overrides.size() == 2, "Reset removes named overrides");
    run(doc, {QJsonObject{{"command", "component_parameter.reset"},
                          {"body", text(t.placement)},
                          {"all", true}}});
    check(doc.instances().at(t.placement)->overrides.empty(), "Reset all removes every override");
    // Rename follows overrides; remove is refused while referenced.
    run(doc, {override(t.placement, {{"width", "1 m"}}),
              QJsonObject{{"command", "component_parameter.rename"},
                          {"definition", text(t.definition)},
                          {"name", "width"},
                          {"newName", "span"}}});
    check(doc.instances().at(t.placement)->overrides.contains("span") &&
              doc.definitions().at(t.definition)->parameters->parameters[2].expression ==
                  "span - 60 mm",
          "Rename rewrites references and overrides");
    run(doc, {QJsonObject{{"command", "component_parameter.update"},
                          {"definition", text(t.definition)},
                          {"name", "rails"},
                          {"label", "Side rails"},
                          {"position", 0}}});
    check(doc.definitions().at(t.definition)->parameters->parameters[0].label == "Side rails",
          "Update changes the label and position");
    run(doc, {QJsonObject{{"command", "component_parameter.remove"},
                          {"definition", text(t.definition)},
                          {"name", "rails"}},
              QJsonObject{{"command", "component_parameter.undrive"},
                          {"definition", text(t.definition)},
                          {"driver", text(t.visibility)}}});
    check(doc.definitions().at(t.definition)->parameters->parameters.size() == 4 &&
              doc.definitions().at(t.definition)->parameters->drivers.size() == 4,
          "Remove and undrive shrink the record");
    std::cout << "atomic batches: ok\n";
}
void refusals() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto valid = define(t.definition, "depth", "Depth", "length", "input", "600 mm");
    refused(doc, {valid, define(t.definition, "x", "X", "length", "input", "900 mm +")},
            "PARAMETER_SYNTAX", "syntax");
    refused(doc, {valid, define(t.definition, "Width", "W", "length", "input", "1 m")},
            "PARAMETER_NAME", "malformed name");
    refused(doc, {valid, define(t.definition, "width", "W", "length", "input", "1 m")},
            "PARAMETER_NAME", "duplicate name");
    refused(doc,
            {valid, QJsonObject{{"command", "component_parameter.remove"},
                                {"definition", text(t.definition)},
                                {"name", "width"}}},
            "PARAMETER_NAME", "remove while referenced");
    refused(doc, {valid, define(t.definition, "x", "X", "length", "input", "true")},
            "PARAMETER_TYPE", "type");
    refused(doc,
            {valid, define(t.definition, "d", "D", "length", "derived", "width + 1 mm"),
             QJsonObject{{"command", "component_parameter.update"},
                         {"definition", text(t.definition)},
                         {"name", "width"},
                         {"expression", "d - 1 mm"}}},
            "PARAMETER_CYCLE", "cycle");
    refused(doc, {valid, override(t.placement, {{"width", "3 m"}})}, "PARAMETER_DOMAIN",
            "override above max");
    refused(doc, {valid, override(t.placement, {{"shelves", 1.5}})}, "PARAMETER_DOMAIN",
            "non-whole count");
    refused(doc, {valid, override(t.placement, {{"width", "1 m + 1 m"}})}, "PARAMETER_SYNTAX",
            "override is one literal");
    refused(doc, {valid, override(t.placement, {{"width", "90 deg"}})}, "PARAMETER_TYPE",
            "override unit mismatch");
    refused(doc, {valid, override(t.placement, {{"leg_x", 1}})}, "PARAMETER_NAME",
            "derived parameters are not overridden");
    const auto root = doc.definitions().at(t.definition)->root;
    refused(doc, {valid, drive(t.definition, "position", root, "width", {{"axis", "x"}})},
            "PARAMETER_DRIVER_TARGET", "root target");
    refused(doc,
            {valid, drive(t.definition, "stretch", t.top, "depth", {{"axis", "y"}, {"at", .3}}),
             override(t.placement, {{"depth", "0 mm"}})},
            "PARAMETER_REGENERATION", "degenerate variant");
    refused(doc,
            {valid, override(t.placement, {{"width", "1.2 m"}}),
             QJsonObject{{"command", "component.edit"},
                         {"definition", text(t.definition)},
                         {"instance", text(t.placement)},
                         {"commands",
                          QJsonArray{QJsonObject{
                              {"command", "geometry.translate"},
                              {"body", text(doc.instances().at(t.placement)->members.at(t.top))},
                              {"delta", QJsonArray{0, 0, .01}}}}}}},
            "PARAMETER_TEMPLATE_SCOPE", "edit through an overridden placement");
    refused(doc,
            {valid, define(t.definition, "x", "X", "length", "input",
                           std::string(1025, '1').c_str())},
            "LIMIT_EXCEEDED", "expression text limit");
    refused(doc,
            {QJsonObject{{"command", "scene.state"}, {"body", text(t.placement)}, {"locked", true}},
             override(t.placement, {{"width", "1 m"}})},
            "LOCKED_ENTITY", "locked placement");
    refused(doc,
            {valid, QJsonObject{{"command", "component_parameter.undrive"},
                                {"definition", text(t.definition)},
                                {"driver", "999"}}},
            "NOT_FOUND", "unknown driver");
    refused(doc, {valid, define(t.definition, "x", "X", "length", "input", "1 m", {{"position", 9}})},
            "INVALID_TARGET", "position outside the list");
    // Request-shape refusals carry no parameter code but still change nothing.
    for (const auto &command :
         {QJsonObject{{"command", "component_parameter.rebind"},
                      {"definition", text(t.definition)}},
          QJsonObject{{"command", "component_parameter.rebind"},
                      {"definition", text(t.definition)},
                      {"all", true},
                      {"drivers", QJsonArray{text(t.stretch)}}},
          drive(t.definition, "stretch", t.top, "width", {{"axis", "x"}}),
          drive(t.definition, "visibility", t.shelf, "true", {{"axis", "x"}})}) {
        const Records before(doc);
        refusal([&] { run(doc, {valid, command}); }, "request shape");
        check(Records(doc) == before, "A malformed request changes nothing");
    }
    // Component edit scopes reject parameter operations.
    for (const auto &nested : {valid, override(t.placement, {{"width", "1 m"}})}) {
        const Records before(doc);
        const auto [code, message] = refusal(
            [&] {
                run(doc, {QJsonObject{{"command", "component.edit"},
                                      {"definition", text(t.definition)},
                                      {"commands", QJsonArray{nested}}}});
            },
            "scoped parameter operation");
        check(message.find("scope") != std::string::npos || message.find("batch") != std::string::npos,
              "A scoped parameter operation is refused: " + message);
        check(Records(doc) == before, "A refused scope changes nothing");
    }
    std::cout << "refusals: ok\n";
}
void rebind() {
    auto t = makeTable();
    auto &doc = t.doc;
    auto state = [&](Id driver) {
        for (const auto &entry :
             query(doc, "component_parameters.describe", {{"definition", text(t.definition)}})
                 ["drivers"]
                     .toArray())
            if (entry.toObject()["id"] == text(driver))
                return entry.toObject();
        throw std::runtime_error("Driver missing from describe");
    };
    check(state(t.stretch)["state"] == "bound", "A new driver is bound");
    const auto stamped = state(t.stretch)["bound"].toString();
    // A template edit that changes the stretched top warns, and does not refuse.
    const auto edited = run(
        doc, {QJsonObject{{"command", "component.edit"},
                          {"definition", text(t.definition)},
                          {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                              {"body", text(t.top)},
                                                              {"delta", QJsonArray{0, 0, .01}}}}}}});
    check(edited["parameterWarnings"].toArray() == QJsonArray{QJsonValue(text(t.stretch))},
          "The template edit lists the driver it moved to changedSinceBound: " +
              json(edited["parameterWarnings"]));
    check(state(t.stretch)["state"] == "changedSinceBound", "describe shows the changed state");
    const auto values = query(doc, "component_parameters.values", {{"body", text(t.placement)}});
    check(values["drivers"].toArray()[0].toObject()["state"] == "changedSinceBound",
          "values shows driver states");
    const auto revision = doc.revision();
    run(doc, {QJsonObject{{"command", "component_parameter.rebind"},
                          {"definition", text(t.definition)},
                          {"drivers", QJsonArray{text(t.stretch)}}}});
    check(doc.revision() == revision + 1 && state(t.stretch)["state"] == "bound" &&
              state(t.stretch)["bound"].toString() != stamped,
          "Rebind restamps the named driver");
    doc.undo();
    check(state(t.stretch)["state"] == "changedSinceBound" &&
              state(t.stretch)["bound"].toString() == stamped,
          "Undo restores the previous stamp");
    run(doc, {QJsonObject{{"command", "component_parameter.rebind"},
                          {"definition", text(t.definition)},
                          {"all", true}}});
    check(state(t.stretch)["state"] == "bound", "Rebind all restamps every driver");
    std::cout << "rebind: ok\n";
}
void inspection() {
    auto t = makeTable();
    auto &doc = t.doc;
    run(doc, {QJsonObject{{"command", "document.units"}, {"units", "mm"}},
              override(t.placement, {{"width", "1200 mm"}})});
    const auto description =
        query(doc, "component_parameters.describe", {{"definition", text(t.definition)}});
    const auto parameters = description["parameters"].toArray();
    check(description["placements"] == 1 && description["overriddenPlacements"] == 1 &&
              parameters.size() == 3 && description["drivers"].toArray().size() == 5,
          "describe lists parameters, drivers and placement counts");
    // Evaluation order: shelves and width are ready first (by name), then leg_x.
    check(parameters[0].toObject()["name"] == "shelves" &&
              parameters[1].toObject()["name"] == "width" &&
              parameters[2].toObject()["name"] == "leg_x" &&
              parameters[2].toObject()["dependsOn"].toArray() == QJsonArray{"width"} &&
              parameters[1].toObject()["usedBy"].toArray().size() == 2,
          "describe gives evaluation order, dependsOn and usedBy: " + json(parameters));
    const auto values = query(doc, "component_parameters.values", {{"body", text(t.placement)}});
    const auto width = values["values"].toArray()[0].toObject();
    check(width["name"] == "width" && width["source"] == "override" &&
              std::abs(width["value"].toDouble() - 1.2) < 1e-12 && width["display"] == "1200 mm",
          "values reports SI value, display and source: " + json(width));
    check(values["values"].toArray()[2].toObject()["source"] == "derived" &&
              values["values"].toArray()[1].toObject()["source"] == "default",
          "Derived and default sources");
    // evaluate is a read-only dry run.
    const Records before(doc);
    const auto evaluated = query(doc, "component_parameters.evaluate",
                                 {{"definition", text(t.definition)},
                                  {"overrides", QJsonObject{{"width", "1500 mm"}, {"shelves", 3}}},
                                  {"expression", "leg_x * 2"}});
    check(evaluated["status"] == "ok" &&
              std::abs(evaluated["expression"].toObject()["value"].toDouble() - 2.88) < 1e-12 &&
              evaluated["expression"].toObject()["type"] == "length" &&
              evaluated["expression"].toObject()["canonical"] == "leg_x * 2" &&
              evaluated["regeneration"].toObject()["generatedMembers"].toInt() > 0,
          "evaluate returns values, the expression and the regeneration size: " +
              json(evaluated));
    const auto syntax = query(doc, "component_parameters.evaluate",
                              {{"definition", text(t.definition)}, {"expression", "width +"}});
    check(syntax["status"] == "error" &&
              syntax["error"].toObject()["code"] == "PARAMETER_SYNTAX" &&
              syntax["error"].toObject()["offset"].toInt() > 0,
          "evaluate reports the first error with its offset: " + json(syntax));
    const auto domain = query(doc, "component_parameters.evaluate",
                              {{"definition", text(t.definition)},
                               {"overrides", QJsonObject{{"width", "5 m"}}}});
    check(domain["status"] == "error" && domain["error"].toObject()["code"] == "PARAMETER_DOMAIN",
          "evaluate checks overrides against bounds");
    const auto boolean = query(doc, "component_parameters.evaluate",
                               {{"definition", text(t.definition)},
                                {"expression", "shelves > 2"},
                                {"type", "boolean"}});
    check(boolean["expression"].toObject()["value"] == false, "evaluate with an explicit type");
    check(Records(doc) == before, "evaluate never changes the document");
    const auto [code, message] = refusal(
        [&] { query(doc, "component_parameters.values", {{"body", "999"}}); }, "values");
    check(code == "NOT_FOUND", "values of an unknown placement is NOT_FOUND");
    std::cout << "inspection: ok\n";
}
// component.edit_instance on a parametric placement promotes its values (ADR 0165 §4).
void promotion() {
    auto t = makeTable();
    auto &doc = t.doc;
    run(doc, {override(t.placement, {{"width", "1200 mm"}})});
    const auto shelf = doc.instances().at(t.placement)->members.at(t.shelf);
    const auto result = run(
        doc, {QJsonObject{{"command", "component.edit_instance"},
                          {"body", text(t.placement)},
                          {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                              {"body", text(shelf)},
                                                              {"delta", QJsonArray{0, 0, .01}}}}}}});
    const auto operation = result["componentOperations"].toArray()[0].toObject();
    check(operation["parametersPromoted"] == true, "edit_instance reports parametersPromoted");
    const auto unique = doc.instances().at(t.placement)->definition;
    check(unique != t.definition && doc.instances().at(t.placement)->overrides.empty() &&
              doc.definitions().at(unique)->parameters->parameters[0].expression == "1.2 m",
          "The unique copy's defaults are the placement's values");
    // A nested placement takes its values from its parent: promotion is refused.
    auto nested = makeTable();
    auto &outer = nested.doc;
    const auto base = box(outer, {2, 0, 0}, {2.1, .1, .1}, "Base");
    const auto assembly = createGroup(outer, {nested.placement, base}, "Assembly");
    run(outer, {QJsonObject{{"command", "component.create"}, {"body", text(assembly)}}});
    const auto nestedShelf = outer.instances().at(nested.placement)->members.at(nested.shelf);
    const Records before(outer);
    const auto [code, message] = refusal(
        [&] {
            run(outer,
                {QJsonObject{{"command", "component.edit_instance"},
                             {"body", text(nested.placement)},
                             {"commands", QJsonArray{QJsonObject{
                                              {"command", "geometry.translate"},
                                              {"body", text(nestedShelf)},
                                              {"delta", QJsonArray{0, 0, .01}}}}}}});
        },
        "nested promotion");
    check(code == "INVALID_TARGET" && Records(outer) == before,
          "edit_instance on a nested parametric placement is refused: " + code + " " + message);
    std::cout << "promotion: ok\n";
}
// The headless session reports parameter codes through its transaction and query APIs.
void session() {
    QTemporaryDir files;
    AutomationSession session({{}, files.path() + "/model.sketchyup", files.path() + "/outcomes", true});
    const auto info = session.execute({{"apiVersion", 1}, {"operation", "session.describe"}});
    const auto [queryCode, queryMessage] = refusal(
        [&] {
            session.execute({{"apiVersion", 1},
                             {"query", "component_parameters.describe"},
                             {"documentId", info["documentId"]},
                             {"expectedRevision", info["revision"]},
                             {"definition", "1"}});
        },
        "session describe");
    check(queryCode == "NOT_FOUND", "The session runs the describe query: " + queryMessage);
    const auto begun = session.execute({{"apiVersion", 1},
                                        {"operation", "transaction.begin"},
                                        {"documentId", info["documentId"]},
                                        {"expectedRevision", info["revision"]}});
    const auto [applyCode, applyMessage] = refusal(
        [&] {
            session.execute(
                {{"apiVersion", 1},
                 {"operation", "transaction.apply"},
                 {"documentId", info["documentId"]},
                 {"transactionId", begun["transactionId"]},
                 {"expectedVersion", 0},
                 {"operationId", "define"},
                 {"commands", QJsonArray{define(1, "width", "Width", "length", "input", "1 m")}}});
        },
        "session apply");
    check(applyCode == "NOT_FOUND",
          "transaction.apply keeps the parameter code: " + applyCode + " " + applyMessage);
    std::cout << "session: ok\n";
}
// Commands, then save and reopen: the values query, the descriptions, the regenerated
// geometry and the canonical encoding survive schema 28 unchanged (R093.c with R093.d).
void persistence() {
    auto t = makeTable();
    auto &doc = t.doc;
    run(doc, {override(t.placement, {{"width", "1200 mm"}, {"shelves", 2}})});
    run(doc, {QJsonObject{{"command", "component.instance"},
                          {"definition", text(t.definition)},
                          {"matrix", QJsonArray{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}}});
    Id copy = 0;
    for (const auto &[id, instance] : doc.instances())
        if (id != t.placement && instance->definition == t.definition)
            copy = id;
    check(copy != 0, "A second placement of the table exists");
    run(doc, {override(copy, {{"width", "600 mm"}})});
    const auto values = [](const Document &value, Id placement) -> QJsonArray {
        return query(value, "component_parameters.values", {{"body", text(placement)}})
            .value("values")
            .toArray();
    };
    const auto describe = [&](const Document &value) {
        return query(value, "component_parameters.describe", {{"definition", text(t.definition)}});
    };
    QTemporaryDir files;
    const auto path = files.filePath("table.sketchyup");
    saveDocument(doc, path);
    check(!doc.dirty(), "Saving a parametric document clears the dirty state");
    auto reopened = loadDocument(path);
    check(values(reopened, t.placement) == values(doc, t.placement) &&
              values(reopened, copy) == values(doc, copy),
          "component_parameters.values round trips through save and reopen: " +
              json(values(reopened, t.placement)));
    const auto width = values(reopened, t.placement)[0].toObject();
    check(width["name"] == "width" && width["source"] == "override" &&
              std::abs(width["value"].toDouble() - 1.2) < 1e-12,
          "The reopened override is 1.2 m: " + json(width));
    check(describe(reopened) == describe(doc),
          "component_parameters.describe round trips through save and reopen");
    check(encodeDocument(reopened) == encodeDocument(doc),
          "The reopened document encodes byte-identically");
    const auto top = [&](const Document &value, Id placement) {
        return maxX(*value.bodies().at(value.instances().at(placement)->members.at(t.top)));
    };
    check(std::abs(top(reopened, t.placement) - top(doc, t.placement)) < 1e-12 &&
              std::abs(top(reopened, copy) - top(doc, copy)) < 1e-12,
          "The reopened placements keep their regenerated geometry");
    // Commands keep working on the reopened document, and a reset saves again.
    run(reopened, {QJsonObject{{"command", "component_parameter.reset"},
                               {"body", text(t.placement)},
                               {"all", true}}});
    const auto reset = values(reopened, t.placement)[0].toObject();
    check(reset["source"] == "default" && std::abs(reset["value"].toDouble() - .9) < 1e-12,
          "Reset after reopen restores the default: " + json(reset));
    saveDocument(reopened, path);
    const auto again = loadDocument(path);
    check(values(again, t.placement) == values(reopened, t.placement) &&
              values(again, copy) == values(doc, copy) &&
              encodeDocument(again) == encodeDocument(reopened),
          "A second save and reopen keeps every value");
    std::cout << "persistence: ok\n";
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        surface();
        atomicBatches();
        refusals();
        rebind();
        inspection();
        promotion();
        session();
        persistence();
        std::cout << "component parameter commands: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
