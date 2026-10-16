// R094.b: the core and command additions the configurable component interface needs
// (ADR 0166 §§1.4, 2, 4.2, 5, 6.2): component_parameter.promote, the assembly identity
// on parameter records, error details, complete dry-run geometry and history labels.
#include "automation/assistant.hpp"
#include "automation/commands.hpp"
#include "automation/component_parameter_commands.hpp"
#include "automation/inspection.hpp"
#include "automation/session.hpp"
#include "component_parameter_fixture.hpp"
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/hosted_components.hpp"
#include "io/document_io.hpp"
#include "io/library_bundle.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QtEndian>
#include <QJsonDocument>
#include <cmath>
#include <functional>
#include <iostream>
using namespace sketchy;
using namespace sketchy::parameter_fixture;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
QString text(Id id) { return QString::number(id); }
QJsonObject request(const Document &doc, QJsonArray commands, QString label = {}) {
    QJsonObject result{{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", commands}};
    if (!label.isEmpty())
        result["history"] = QJsonObject{{"label", label}};
    return result;
}
QJsonObject run(Document &doc, QJsonArray commands, QString label = {}) {
    return executeBatch(doc, request(doc, std::move(commands), std::move(label)));
}
QJsonObject query(const Document &doc, const char *name, QJsonObject extra) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return inspectDocument(doc, extra)["data"].toObject();
}
// The refusal an operation raises: its code and details.
InspectionError refusal(const std::function<void()> &operation, const std::string &label) {
    try {
        operation();
    } catch (const InspectionError &error) {
        ++checks;
        return error;
    } catch (const std::exception &error) {
        throw std::runtime_error(label + ": expected a coded refusal, got: " + error.what());
    }
    throw std::runtime_error(label + ": expected a refusal, but the operation succeeded");
}
// Every model record table by pointer, the allocators, revision, history and dirty
// state.
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
QJsonObject promote(Id body) {
    return {{"command", "component_parameter.promote"}, {"body", text(body)}};
}
QJsonObject override(Id body, QJsonObject values) {
    return {{"command", "component_parameter.override"}, {"body", text(body)}, {"values", values}};
}
std::string latestLabel(const Document &doc) {
    const auto page = doc.history();
    for (auto it = page.entries.rbegin(); it != page.entries.rend(); ++it)
        if (it->applied)
            return it->label;
    return {};
}
// The scene geometry of one placement: each member's canonical record and world
// transform, keyed by scene ID.
std::map<Id, std::pair<std::string, Transform>> placementGeometry(const Document &doc, Id root) {
    std::map<Id, std::pair<std::string, Transform>> result;
    for (const auto &[member, id] : doc.instances().at(root)->members) {
        std::string bytes;
        appendCanonicalBody(bytes, *doc.bodies().at(id));
        result[id] = {bytes, doc.worldTransform(id)};
    }
    return result;
}
std::map<std::string, QJsonValue> valuesByName(const QJsonArray &values, const char *field) {
    std::map<std::string, QJsonValue> result;
    for (const auto &value : values)
        result[value.toObject()["name"].toString().toStdString()] = value.toObject()[field];
    return result;
}
bool hasAssembly(const Document &doc, Id definition) {
    const auto &record = doc.definitions().at(definition)->parameters;
    return record && record->assembly;
}

// ---- component_parameter.promote (ADR 0166 §4.2) ----
void promotion() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto before = Records(doc);
    const auto geometry = placementGeometry(doc, t.wide);
    const auto values = valuesByName(
        query(doc, "component_parameters.values", {{"body", text(t.wide)}})["values"].toArray(),
        "value");
    const auto defaultsGeometry = placementGeometry(doc, t.defaults);
    const auto result = run(doc, {promote(t.wide)}, "Make Table unique");
    const auto copy = doc.instances().at(t.wide)->definition;
    check(copy != t.definition && !before.definitions.contains(copy),
          "Promotion makes a shared placement unique");
    check(doc.history().total == before.history + 1 && latestLabel(doc) == "Make Table unique",
          "Promotion is one labelled History entry");
    check(doc.instances().at(t.wide)->overrides.empty(), "Promotion clears the overrides");
    const auto described = query(doc, "component_parameters.describe", {{"definition", text(copy)}});
    const auto defaults = valuesByName(described["parameters"].toArray(), "defaultValue");
    check(defaults == values, "The copy's defaults equal the placement's former values");
    check(placementGeometry(doc, t.wide) == geometry, "Promotion keeps the placement's geometry");
    const auto sources = valuesByName(
        query(doc, "component_parameters.values", {{"body", text(t.wide)}})["values"].toArray(),
        "source");
    check(std::none_of(sources.begin(), sources.end(),
                       [](const auto &entry) { return entry.second == "override"; }),
          "The promoted placement takes every value from its defaults");
    check(placementGeometry(doc, t.defaults) == defaultsGeometry &&
              doc.definitions().at(t.definition) == before.definitions.at(t.definition),
          "Other placements and the shared definition are unchanged");
    const auto operation = result["componentOperations"].toArray().last().toObject();
    check(operation["command"] == "component_parameter.promote" && operation["madeUnique"] == true &&
              operation["parametersPromoted"] == true && operation["definition"] == text(copy) &&
              operation["instance"] == text(t.wide),
          "The batch reports the promotion");
    doc.undo();
    check(Records(doc).definitions == before.definitions &&
              Records(doc).instances == before.instances && Records(doc).bodies == before.bodies,
          "One Undo restores every record");
    doc.redo();
    check(doc.instances().at(t.wide)->definition == copy, "Redo repeats the promotion");

    // An unshared placement is promoted in place and keeps its assembly identity.
    {
        auto u = makeTable();
        const auto room = u.doc.instances().at(u.roomPlacement)->definition;
        const auto roomGeometry = placementGeometry(u.doc, u.roomPlacement);
        const auto promoted = run(u.doc, {promote(u.roomPlacement)});
        const auto operation = promoted["componentOperations"].toArray().last().toObject();
        check(operation["madeUnique"] == false &&
                  u.doc.instances().at(u.roomPlacement)->definition == room &&
                  u.doc.instances().at(u.roomPlacement)->overrides.empty() &&
                  hasAssembly(u.doc, room),
              "An unshared placement is promoted in place and keeps its identity");
        check(placementGeometry(u.doc, u.roomPlacement) == roomGeometry,
              "In-place promotion keeps the geometry, nested table included");
        // A second room placement makes the definition shared: the copy has no identity.
        const auto second =
            placeComponent(u.doc, room, Transform::translation({20, 0, 0})).instance;
        overrideComponentParameters(u.doc, second, {{"table_width", metres(1.4)}});
        run(u.doc, {promote(second)});
        const auto roomCopy = u.doc.instances().at(second)->definition;
        check(roomCopy != room && !hasAssembly(u.doc, roomCopy) && hasAssembly(u.doc, room),
              "Promotion through make-unique drops the identity from the copy only");
    }

    // Refusals leave every record unchanged and name their cause.
    {
        auto r = makeTable();
        const Records unchanged(r.doc);
        auto refused = [&](Id body, const std::string &code, const std::string &label) {
            const auto error = refusal([&] { run(r.doc, {promote(body)}); }, label);
            check(error.code() == code, label + " is " + code + ", not " + error.code());
            check(Records(r.doc) == unchanged, label + " leaves the document unchanged");
            return error;
        };
        refused(r.nested, "INVALID_TARGET", "Promoting a nested placement");
        refused(999999, "NOT_FOUND", "Promoting a missing placement");
        const auto plain = r.doc.addFace({{{30, 0, 0}, {31, 0, 0}, {31, 1, 0}}}, "Plain");
        const auto component = createComponent(r.doc, plain, "Plain");
        const Records withPlain(r.doc);
        const auto error = refusal([&] { run(r.doc, {promote(component.instance)}); },
                                   "Promoting a placement without parameters");
        check(error.code() == "INVALID_TARGET" && Records(r.doc) == withPlain,
              "A placement without parameters cannot be promoted");
        setEntityState(r.doc, r.wide, {}, true);
        const Records locked(r.doc);
        const auto lockedError =
            refusal([&] { run(r.doc, {promote(r.wide)}); }, "Promoting a locked placement");
        check(lockedError.code() == "LOCKED_ENTITY" &&
                  lockedError.details()["body"] == text(r.wide) && Records(r.doc) == locked,
              "A locked placement is refused with LOCKED_ENTITY naming it");
    }

    // The name keeps the operation from the assistant (the prefix rule, ADR 0165 §6).
    {
        AssistantTask::Options options;
        options.prompt = "Make the table unique.";
        options.provider = "fixture";
        options.model = "fixture";
        options.allowedCommands = {"component_parameter.promote"};
        const auto error = refusal(
            [&] {
                AssistantTask task(
                    AssistantBackend{[](const QJsonObject &) { return QJsonObject{}; },
                                     [] {
                                         return QJsonObject{{"documentId", "doc"},
                                                            {"revision", "1"}};
                                     }},
                    options);
            },
            "assistant authorization of promote");
        check(error.code() == "UNSUPPORTED_CAPABILITY",
              "The assistant cannot be authorized to promote");
        bool listed = false;
        for (const auto &entry : commandCatalog())
            listed = listed || entry.toObject()["name"] == "component_parameter.promote";
        check(listed, "The command catalogue lists component_parameter.promote");
    }
    std::cout << "promotion: ok\n";
}

// ---- The assembly identity (ADR 0166 §6.2) ----
QByteArray repack(const QByteArray &container, const QByteArray &document) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    auto head = QJsonDocument::fromJson(container.mid(16, length)).object();
    auto chunks = head["chunks"].toArray();
    auto chunk = chunks[0].toObject();
    chunk["bytes"] = QString::number(document.size());
    chunk["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(document, QCryptographicHash::Sha256).toHex());
    chunks[0] = chunk;
    head["chunks"] = chunks;
    const auto encoded = QJsonDocument(head).toJson(QJsonDocument::Compact);
    auto header = container.first(16);
    qToLittleEndian<quint32>(encoded.size(), header.data() + 12);
    return header + encoded + document;
}
QJsonObject documentTree(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto head = QJsonDocument::fromJson(container.mid(16, length)).object();
    const auto bytes = head["chunks"].toArray()[0].toObject()["bytes"].toString().toLongLong();
    return QJsonDocument::fromJson(container.mid(16 + length, bytes)).object();
}
void assemblyIdentity() {
    auto t = makeTable();
    auto &doc = t.doc;
    check(doc.definitions().at(t.room)->parameters->assembly ==
              ComponentAssembly{"sketchyup-tests/room", 1},
          "The fixture's room has an assembly identity");
    // Round trip.
    const auto saved = encodeContainer(doc);
    const auto loaded = decodeContainer(saved);
    check(*loaded.definitions().at(t.room)->parameters == *doc.definitions().at(t.room)->parameters &&
              !loaded.definitions().at(t.definition)->parameters->assembly,
          "The identity round trips and stays off records without one");
    check(encodeContainer(loaded).mid(16) == saved.mid(16) || documentTree(encodeContainer(loaded)) ==
                                                                  documentTree(saved),
          "A reloaded document re-encodes the identity exactly");
    // The writer stores it only where set.
    const auto tree = documentTree(saved);
    int identified = 0;
    for (const auto &value : tree["componentParameters"].toArray()) {
        const auto record = value.toObject();
        if (record.contains("assembly")) {
            ++identified;
            check(record["definition"] == text(t.room) &&
                      record["assembly"] == QJsonObject{{"id", "sketchyup-tests/room"},
                                                        {"version", 1}},
                  "The stored identity is {id, version}");
        }
    }
    check(identified == 1, "Only the room's record stores an identity");
    // Malformed identities are refused on read, and the file is not modified.
    const QList<QJsonValue> malformed{
        QJsonValue::Null,
        QJsonObject{{"id", "Room"}, {"version", 1}},
        QJsonObject{{"id", "a/b/c"}, {"version", 1}},
        QJsonObject{{"id", QString(65, 'a')}, {"version", 1}},
        QJsonObject{{"id", ""}, {"version", 1}},
        QJsonObject{{"id", "room/"}, {"version", 1}},
        QJsonObject{{"id", "room"}, {"version", 0}},
        QJsonObject{{"id", "room"}, {"version", 1.5}},
        QJsonObject{{"id", "room"}, {"version", "1"}},
        QJsonObject{{"id", "room"}, {"version", 2147483648.0}},
        QJsonObject{{"id", "room"}},
        QJsonObject{{"id", "room"}, {"version", 1}, {"extra", true}},
        QJsonArray{"room", 1}};
    for (const auto &value : malformed) {
        auto root = tree;
        auto records = root["componentParameters"].toArray();
        for (qsizetype i = 0; i < records.size(); ++i) {
            auto record = records[i].toObject();
            if (record["definition"] == text(t.room)) {
                record["assembly"] = value;
                records[i] = record;
            }
        }
        root["componentParameters"] = records;
        const auto forged = repack(saved, QJsonDocument(root).toJson(QJsonDocument::Compact));
        bool refused = false;
        try {
            (void)decodeContainer(forged);
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "A malformed identity is refused: " +
                           QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)
                               .toStdString());
    }
    // A well-formed identity with a two-segment id and the largest version loads.
    {
        auto root = tree;
        auto records = root["componentParameters"].toArray();
        for (qsizetype i = 0; i < records.size(); ++i) {
            auto record = records[i].toObject();
            if (record["definition"] == text(t.definition)) {
                record["assembly"] =
                    QJsonObject{{"id", QString(64, 'z') + "/" + QString(64, '_')},
                                {"version", 2147483647.0}};
                records[i] = record;
            }
        }
        root["componentParameters"] = records;
        const auto accepted =
            decodeContainer(repack(saved, QJsonDocument(root).toJson(QJsonDocument::Compact)));
        check(accepted.definitions().at(t.definition)->parameters->assembly->version ==
                  componentAssemblyMaxVersion,
              "The largest identity loads");
    }
    // The core builder refuses malformed identities and is one edit.
    {
        const Records unchanged(doc);
        bool refused = false;
        try {
            setComponentAssembly(doc, t.definition, ComponentAssembly{"Bad Id", 1});
        } catch (const ComponentParameterError &error) {
            refused = error.code() == "PARAMETER_NAME";
        }
        check(refused && Records(doc) == unchanged, "The builder refuses a malformed identity");
        setComponentAssembly(doc, t.definition, ComponentAssembly{"sketchyup/table", 2});
        check(doc.history().total == unchanged.history + 1 &&
                  doc.definitions().at(t.definition)->parameters->assembly->version == 2,
              "Setting an identity is one edit");
        doc.undo();
    }
    // Bundle capture keeps it; make-unique (as a command) leaves it off the copy.
    {
        const auto png = encodeTexturePng(TextureImage(1, 1, {10, 20, 30, 255}));
        const auto bundle = decodeComponentBundle(
            encodeComponentBundle(doc, t.room, {"Room", "Parametric room", {"test"}, 0}, png));
        check(bundle.document.definitions().at(t.room)->parameters->assembly ==
                  doc.definitions().at(t.room)->parameters->assembly,
              "A component bundle captures the identity");
        const auto result = run(doc, {QJsonObject{{"command", "component.make_unique"},
                                                  {"body", text(t.roomPlacement)}}});
        const auto copy = doc.instances().at(t.roomPlacement)->definition;
        check(copy != t.room && !hasAssembly(doc, copy), "Make-unique drops the identity");
        // The copied nested table never had one; the original room record keeps its own.
        check(doc.definitions().at(t.room)->parameters->assembly.has_value(),
              "The original definition keeps its identity");
        (void)result;
    }
    std::cout << "assembly identity: ok\n";
}

// ---- Error details (ADR 0166 §5) ----
void errorDetails() {
    auto t = makeTable();
    auto &doc = t.doc;
    const Records unchanged(doc);
    // The violated bound, its value and the refused value.
    auto bound = [&](const char *value, const char *which, double limit, double refused) {
        const auto error = refusal([&] { run(doc, {override(t.wide, {{"width", value}})}); },
                                   std::string("width ") + value);
        const auto details = error.details();
        check(error.code() == "PARAMETER_DOMAIN" && details["bound"] == which &&
                  std::abs(details["limit"].toDouble() - limit) < 1e-12 &&
                  std::abs(details["value"].toDouble() - refused) < 1e-9 &&
                  details["parameter"] == "width" && details["body"] == text(t.wide) &&
                  details["definition"] == text(t.definition),
              std::string("The ") + which + " bound is named in the details");
        check(Records(doc) == unchanged, "The refused value changes nothing");
    };
    bound("2.6 m", "max", 2.4, 2.6);
    bound("300 mm", "min", .4, .3);
    {
        const auto error = refusal(
            [&] { run(doc, {override(t.wide, {{"tilt", "45 deg"}})}); }, "tilt above its max");
        check(error.details()["bound"] == "max", "Angle bounds are named too");
    }
    // The failure object of the sessions and MCP carries them.
    {
        const auto error =
            refusal([&] { run(doc, {override(t.wide, {{"width", "2.6 m"}})}); }, "failure");
        const auto failure = automationFailure(error);
        check(failure["code"] == "PARAMETER_DOMAIN" &&
                  failure["details"].toObject()["bound"] == "max",
              "automationFailure reports the details");
        const auto plain = automationFailure(InspectionError("NOT_FOUND", "x"));
        check(!plain.contains("details"), "Errors without details keep their shape");
    }
    // LOCKED_ENTITY names the locked placement, on an override and on a definition edit.
    setEntityState(doc, t.defaults, {}, true);
    {
        const Records locked(doc);
        const auto onOverride = refusal(
            [&] { run(doc, {override(t.defaults, {{"width", "1 m"}})}); }, "locked override");
        check(onOverride.code() == "LOCKED_ENTITY" &&
                  onOverride.details()["body"] == text(t.defaults),
              "LOCKED_ENTITY from an override names the placement");
        const auto onDefinition = refusal(
            [&] {
                run(doc, {QJsonObject{{"command", "component_parameter.update"},
                                      {"definition", text(t.definition)},
                                      {"name", "width"},
                                      {"expression", "1 m"}}});
            },
            "locked definition edit");
        check(onDefinition.code() == "LOCKED_ENTITY" &&
                  onDefinition.details()["body"] == text(t.defaults),
              "LOCKED_ENTITY from a default change names the locked placement");
        check(Records(doc) == locked, "The locked refusals change nothing");
    }
    // evaluate returns the same details as data.
    {
        const auto evaluated =
            query(doc, "component_parameters.evaluate",
                  {{"definition", text(t.definition)},
                   {"overrides", QJsonObject{{"width", "2.6 m"}}}});
        const auto error = evaluated["error"].toObject();
        check(evaluated["status"] == "error" && error["code"] == "PARAMETER_DOMAIN" &&
                  error["bound"] == "max" && std::abs(error["limit"].toDouble() - 2.4) < 1e-12,
              "evaluate reports the violated bound");
    }
    // R093.d's optional `type` already checks an expression against a target type,
    // so no `for` field is added (ADR 0166 §3.5).
    {
        auto evaluate = [&](const char *expression, const char *type) {
            return query(doc, "component_parameters.evaluate",
                         {{"definition", text(t.definition)},
                          {"expression", expression},
                          {"type", type}});
        };
        const auto bare = evaluate("width - 120", "length");
        check(bare["status"] == "error" && bare["error"].toObject()["code"] == "PARAMETER_TYPE" &&
                  bare["error"].toObject()["offset"].toInt() > 0,
              "A bare number against a length is PARAMETER_TYPE with an offset");
        const auto fixed = evaluate("width - 120 mm", "length");
        check(fixed["status"] == "ok" &&
                  fixed["expression"].toObject()["type"] == "length",
              "The unit fix checks clean");
        const auto angle = evaluate("width", "angle");
        check(angle["status"] == "error" && angle["error"].toObject()["code"] == "PARAMETER_TYPE",
              "A length is not an angle");
        const auto visible = evaluate("shelves > 0", "boolean");
        check(visible["status"] == "ok", "A visibility expression checks as a boolean");
    }
    std::cout << "error details: ok\n";
}

// ---- Dry-run geometry (ADR 0166 §2) ----
// The preview of `commands` must list exactly what committing them changes.
void sameAsCommit(const Document &doc, const QJsonArray &commands, const std::string &label,
                  const std::function<void(const QJsonObject &, const Document &)> &extra = {}) {
    const Records unchanged(doc);
    const auto preview = previewBatch(doc, request(doc, commands));
    check(Records(doc) == unchanged,
          label + ": the dry run leaves revision, history and records pointer-identical");
    check(preview["status"] == "preview" && preview["baseRevision"] == text(doc.revision()),
          label + ": a preview result");
    Document committed = doc;
    executeBatch(committed, request(committed, commands));
    auto shown = [](const Document &document, Id id) {
        for (; id; id = document.bodies().at(id)->parent)
            if (document.bodies().at(id)->hidden)
                return false;
        return true;
    };
    std::set<QString> changed, removed;
    for (const auto &[id, body] : committed.bodies())
        if ((!doc.bodies().contains(id) || doc.bodies().at(id) != body ||
             doc.worldTransform(id) != committed.worldTransform(id)) &&
            shown(committed, id))
            changed.insert(text(id));
    for (const auto &[id, body] : doc.bodies())
        if (shown(doc, id) && (!committed.bodies().contains(id) || !shown(committed, id)))
            removed.insert(text(id));
    const auto geometry = preview["geometry"].toObject();
    const auto keys = geometry.keys();
    check(std::set<QString>(keys.begin(), keys.end()) == changed,
          label + ": geometry holds every changed body that is shown");
    std::set<QString> listed;
    for (const auto &id : preview["removed"].toArray())
        listed.insert(id.toString());
    check(listed == removed, label + ": removed lists every body deleted or hidden");
    for (const auto &key : keys) {
        const auto id = key.toULongLong();
        QJsonArray world;
        for (const auto value : committed.worldTransform(id).m)
            world.append(value);
        check(geometry[key].toObject()["worldTransform"].toArray() == world &&
                  geometry[key].toObject()["faces"].toArray().size() ==
                      qsizetype(committed.bodies().at(id)->surface.faces.size()),
              label + ": each body carries its committed faces and world transform");
    }
    if (extra)
        extra(preview, committed);
}
void dryRun() {
    auto t = makeTable();
    const auto &doc = t.doc;
    const auto members = doc.instances().at(t.wide)->members;
    const auto definition = doc.definitions().at(t.definition);
    // Wider with a fourth shelf: the top, the right legs and a new repeat copy.
    sameAsCommit(doc, {override(t.wide, {{"width", "1.5 m"}, {"shelves", 4}})}, "wider",
                 [&](const QJsonObject &preview, const Document &committed) {
                     const auto geometry = preview["geometry"].toObject();
                     for (const auto member : {t.top, t.legRightFront, t.legRightBack})
                         check(geometry.contains(text(members.at(member))),
                               "The dry run includes every driven member");
                     int copies = 0;
                     for (const auto &[id, body] : committed.bodies())
                         copies += !doc.bodies().contains(id) && geometry.contains(text(id));
                     check(copies == 1, "The dry run includes the new repeat copy");
                 });
    // No shelves: the shelf is hidden and its copies removed.
    sameAsCommit(doc, {override(t.wide, {{"shelves", 0}})}, "no shelves",
                 [&](const QJsonObject &preview, const Document &) {
                     const auto removed = preview["removed"].toArray();
                     check(removed.contains(text(members.at(t.shelf))) && removed.size() == 3,
                           "The hidden shelf and both copies are reported as removed");
                     check(!preview["geometry"].toObject().contains(text(members.at(t.shelf))),
                           "A hidden member has no drawable geometry");
                 });
    // A default change regenerates every placement, the nested table included.
    sameAsCommit(doc,
                 {QJsonObject{{"command", "component_parameter.update"},
                              {"definition", text(t.definition)},
                              {"name", "width"},
                              {"expression", "1 m"}}},
                 "default change");
    (void)definition;
    // A hosted placement: the host's re-cut faces are part of the dry run.
    {
        Document hosted;
        auto wall = hosted.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
        hosted.extrude(wall, hosted.bodies().at(wall)->surface.faces.begin()->first, 1);
        Id top = 0;
        for (const auto &[id, face] : hosted.bodies().at(wall)->surface.faces)
            if (hosted.bodies().at(wall)->surface.normal(id).z > .99)
                top = id;
        const auto body = hosted.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto reference = hosted.bodies().at(body)->surface.faces.begin()->first;
        const auto window = createComponent(hosted, body, "Window");
        const auto member = window.movedGeometry.at(body);
        setComponentGlue(hosted, window.definition,
                         ComponentGlue{member, reference, {1, 1, 0}, {1, 0, 0}, true});
        defineComponentParameter(hosted, window.definition,
                                 parameter("width", "Width", ParameterType::Length,
                                           ComponentParameterMode::Input, "2 m"));
        auto stretch = driver(ComponentDriverKind::Stretch, member, "width");
        stretch.at = 1;
        driveComponent(hosted, window.definition, 0, stretch);
        attachComponent(hosted, window.instance, wall, top, {{3, 3, 1}});
        sameAsCommit(hosted, {override(window.instance, {{"width", "3 m"}})}, "hosted re-cut",
                     [&](const QJsonObject &preview, const Document &committed) {
                         check(committed.bodies().at(wall) != hosted.bodies().at(wall) &&
                                   preview["geometry"].toObject().contains(text(wall)),
                               "The dry run includes the re-cut host");
                     });
    }
    // A failing dry run refuses like the commit and changes nothing.
    {
        const Records unchanged(doc);
        const auto error = refusal(
            [&] { (void)previewBatch(doc, request(doc, {override(t.wide, {{"width", "9 m"}})})); },
            "failing preview");
        check(error.code() == "PARAMETER_DOMAIN" && error.details()["bound"] == "max" &&
                  Records(doc) == unchanged,
              "A failing dry run reports the commit's error and changes nothing");
    }
    std::cout << "dry run: ok\n";
}

// ---- History labels (ADR 0166 §1.4) ----
void historyLabels() {
    auto t = makeTable();
    auto &doc = t.doc;
    run(doc, {override(t.wide, {{"width", "1.3 m"}})}, "Change Width");
    check(latestLabel(doc) == "Change Width", "A batch takes the History label it is given");
    run(doc, {override(t.wide, {{"width", "1.4 m"}})});
    check(latestLabel(doc) == "Set component parameter values on a placement",
          "Without a label a batch keeps the command's catalogue label");
    run(doc, {QJsonObject{{"command", "component_parameter.reset"},
                          {"body", text(t.wide)},
                          {"all", true}}},
        "Reset all parameters");
    check(latestLabel(doc) == "Reset all parameters" && doc.instances().at(t.wide)->overrides.empty(),
          "Reset all is one labelled entry");
    std::cout << "history labels: ok\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        promotion();
        assemblyIdentity();
        errorDetails();
        dryRun();
        historyLabels();
        std::cout << checks << " checks; component parameter desktop additions passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
