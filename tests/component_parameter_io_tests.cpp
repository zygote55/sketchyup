// R093.c: schema-28 component parameter persistence, the writer policy and the
// previous-release window of ADR 0165 §5.
//   component_parameter_io_tests [--write-fixture PATH]
#include "component_parameter_fixture.hpp"
#include "core/component_regeneration.hpp"
#include "core/drawing_sheets.hpp"
#include "io/component_library.hpp"
#include "io/component_parameters_io.hpp"
#include "io/document_io.hpp"
#include "io/library_bundle.hpp"
#include "io/native_format.hpp"
#include "io/recovery.hpp"
#include "io/texture_image.hpp"
#include "sketchyup/version.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <chrono>
#include <functional>
#include <iostream>
#include <set>
#include <string>
using namespace sketchy;
using namespace sketchy::parameter_fixture;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
std::string rejection(const std::function<void()> &operation, const std::string &what) {
    try {
        operation();
    } catch (const std::exception &error) {
        ++checks;
        return error.what();
    }
    throw std::runtime_error("Expected rejection: " + what);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + path.toStdString());
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
          "Write " + path.toStdString());
}
QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonObject manifest(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    return QJsonDocument::fromJson(container.mid(16, length)).object();
}
QByteArray documentChunk(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto bytes =
        manifest(container)["chunks"].toArray()[0].toObject()["bytes"].toString().toLongLong();
    return container.mid(16 + length, bytes);
}
QJsonObject documentTree(const QByteArray &container) {
    return QJsonDocument::fromJson(documentChunk(container)).object();
}
int documentVersion(const QByteArray &container) { return documentTree(container)["version"].toInt(); }
QByteArray repack(const QByteArray &container, QJsonObject head, const QByteArray &document) {
    auto chunks = head["chunks"].toArray();
    auto chunk = chunks[0].toObject();
    chunk["bytes"] = QString::number(document.size());
    chunk["sha256"] = sha256(document);
    chunks[0] = chunk;
    head["chunks"] = chunks;
    const auto encoded = QJsonDocument(head).toJson(QJsonDocument::Compact);
    auto header = container.first(16);
    qToLittleEndian<quint32>(encoded.size(), header.data() + 12);
    return header + encoded + document;
}
QByteArray compact(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
bool emptyArray(const QJsonValue &value) { return value.isArray() && value.toArray().isEmpty(); }
void swapFirst(QJsonArray &list) {
    const auto first = list[0];
    list[0] = list[1];
    list[1] = first;
}
std::set<QString> keys(const QJsonObject &object) {
    const auto list = object.keys();
    return {list.begin(), list.end()};
}
// Applies `mutate` to the object reached from `root` by a path of keys and indices.
void at(QJsonObject &root, const QStringList &path,
        const std::function<void(QJsonObject &)> &mutate) {
    std::function<QJsonValue(QJsonValue, qsizetype)> descend = [&](QJsonValue node,
                                                                   qsizetype step) -> QJsonValue {
        if (step == path.size()) {
            auto object = node.toObject();
            mutate(object);
            return object;
        }
        bool numeric{};
        const auto index = path[step].toInt(&numeric);
        if (numeric) {
            auto list = node.toArray();
            list[index] = descend(list[index], step + 1);
            return list;
        }
        auto object = node.toObject();
        object[path[step]] = descend(object[path[step]], step + 1);
        return object;
    };
    root = descend(root, 0).toObject();
}
QJsonValue node(const QJsonObject &root, const QStringList &path) {
    QJsonValue value = root;
    for (const auto &step : path) {
        bool numeric{};
        const auto index = step.toInt(&numeric);
        value = numeric ? value.toArray()[index] : value.toObject()[step];
    }
    return value;
}
const ComponentParameters &record(const Document &doc, Id definition) {
    return *doc.definitions().at(definition)->parameters;
}
// Parameter records, overrides and the driver allocator are equal.
void sameParameters(const Document &a, const Document &b, const std::string &label) {
    check(a.definitions().size() == b.definitions().size(), label + ": definitions");
    for (const auto &[id, definition] : a.definitions()) {
        const auto &other = b.definitions().at(id)->parameters;
        check(bool(definition->parameters) == bool(other) &&
                  (!other || *definition->parameters == *other),
              label + ": parameter record of definition " + std::to_string(id));
    }
    check(a.instances().size() == b.instances().size(), label + ": placements");
    for (const auto &[root, instance] : a.instances())
        check(b.instances().at(root)->overrides == instance->overrides &&
                  b.instances().at(root)->members == instance->members,
              label + ": overrides and bindings of placement " + std::to_string(root));
    check(a.nextComponentDriverId() == b.nextComponentDriverId(), label + ": driver allocator");
    check(sameComponentVariants(a.componentVariants(), b.componentVariants()),
          label + ": the same variants");
}
double maxX(const Document &doc, Id placement, Id member) {
    const auto id = doc.instances().at(placement)->members.at(member);
    double result = -1e9;
    for (const auto &[vertex, point] : doc.bodies().at(id)->surface.vertices)
        result = std::max(result, point.x);
    return result;
}
int runPriorCli(const QString &cli, const QStringList &arguments, QByteArray &out,
                QByteArray &err) {
    QProcess process;
    process.start(cli, arguments);
    check(process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit,
          "Prior release CLI runs");
    out = process.readAllStandardOutput();
    err = process.readAllStandardError();
    return process.exitCode();
}
// ---- Load budget (ADR 0165 §7 "Budget"): the worst accepted documents. ----
// Ten times the values R093.e measured on its reference machine, per build: the
// sanitized build runs several times slower, so it has its own ceiling.
#if defined(__SANITIZE_ADDRESS__)
constexpr double loadCeilingSeconds = 490; // measured at most 48.2 s
#else
constexpr double loadCeilingSeconds = 96; // measured at most 9.5 s
#endif
struct Panel {
    Document doc;
    Id definition{}, placement{}, member{};
};
Panel makePanel(Document doc, double x) {
    Panel p{std::move(doc)};
    const auto face =
        p.doc.addFace({{{x, 0, 0}, {x + 1, 0, 0}, {x + 1, 1, 0}, {x, 1, 0}}}, "Panel");
    const auto created = createComponent(p.doc, face, "Panel");
    p.definition = created.definition;
    p.placement = created.instance;
    p.member = created.movedGeometry.at(face);
    return p;
}
std::string chain(std::size_t terms) {
    std::string text = "-x";
    for (std::size_t i = 1; i < terms; ++i)
        text += " + x";
    return text;
}
ComponentParameter numberParameter(std::string name, std::string expression,
                                   ComponentParameterMode mode = ComponentParameterMode::Derived) {
    ComponentParameter parameter;
    parameter.name = std::move(name);
    parameter.label = "N";
    parameter.type = ParameterType::Number;
    parameter.mode = mode;
    parameter.expression =
        canonicalParameterExpression(parseParameterExpression(std::move(expression)));
    return parameter;
}
DefinitionPtr withRecord(const DefinitionPtr &definition, ComponentParameters record) {
    auto copy = std::make_shared<ComponentDefinition>(*definition);
    copy->parameters = std::make_shared<const ComponentParameters>(std::move(record));
    return copy;
}
// Copies of a one-member placement with the given overrides, rebuilt by restore (as
// the R093.b limit tests do), projected through their variants when `project` is set.
Document withPlacements(const Document &source, Id prototype,
                        const std::vector<ComponentOverrides> &values,
                        ComponentDefinitions definitions, bool project) {
    auto bodies = source.bodies();
    auto instances = source.instances();
    Id next = source.nextId();
    const auto &binding = *source.instances().at(prototype);
    const auto &definition = *definitions.at(binding.definition);
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto instance = std::make_shared<ComponentInstance>(binding);
        instance->overrides = values[i];
        std::map<Id, Id> remap;
        for (auto &[member, target] : instance->members) {
            remap[target] = next;
            target = next++;
        }
        auto root = std::make_shared<Body>(*source.bodies().at(prototype));
        root->id = instance->members.at(definition.root);
        root->transform =
            Transform::translation({2.0 * double(i % 100), 2.0 * double(i / 100) + 3, 0});
        instances[root->id] = instance;
        if (project) {
            for (auto &[id, body] :
                 projectComponentPlacement(definitions, instances, root->id, *root))
                bodies[id] = body;
            continue;
        }
        bodies[root->id] = root;
        for (const auto &[member, target] : binding.members) {
            if (target == prototype)
                continue;
            auto body = std::make_shared<Body>(*source.bodies().at(target));
            body->id = remap.at(target);
            body->parent = remap.at(body->parent);
            bodies[body->id] = body;
        }
    }
    Document doc;
    doc.restore(source.identity(), next, std::move(bodies), source.revision(),
                std::move(definitions), std::move(instances), source.nextDefinitionId(),
                source.tags(), source.nextTagId(), source.materials(), source.nextMaterialId(),
                source.assets(), source.nextAssetId(), source.displayUnits(),
                source.hostedRecords(), source.style(), source.scenes(), source.nextSceneId(),
                source.sections(), source.nextSectionId(), source.activeSections(),
                source.annotations(), source.nextAnnotationId(), source.solar(),
                source.displayPrecision(), {}, 1, {}, 1, {}, 1, source.nextComponentDriverId());
    return doc;
}
// Seconds to decode `bytes` as a container (the reader's checks, the evaluation of
// every placement and restore), the best of `runs`.
double loadSeconds(const QByteArray &bytes, const Document &expected, int runs) {
    double best = 1e9;
    for (int run = 0; run < runs; ++run) {
        const auto started = std::chrono::steady_clock::now();
        const auto loaded = decodeContainer(bytes);
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                            started)
                                  .count());
        check(loaded.componentVariants().visits == expected.componentVariants().visits &&
                  loaded.componentVariants().variants.size() ==
                      expected.componentVariants().variants.size(),
              "The worst document reloads with the same evaluation");
    }
    return best;
}
void loadBudget() {
    // (1) Evaluation: one definition of 125 parameters with 256-node formulas and 629
    // distinct placements of it, plus a filler definition, total exactly the document
    // budget of 20,000,000 evaluation visits.
    auto p = makePanel(Document{}, 0);
    auto second = makePanel(std::move(p.doc), 5);
    p.doc = std::move(second.doc);
    ComponentParameters heavy;
    heavy.parameters.push_back(numberParameter("x", "1", ComponentParameterMode::Input));
    for (int i = 1; i <= 124; ++i)
        heavy.parameters.push_back(numberParameter("q" + std::to_string(i), chain(128)));
    ComponentParameters filler;
    filler.parameters.push_back(numberParameter("x", "1", ComponentParameterMode::Input));
    for (int i = 1; i <= 5; ++i) {
        std::string text = "x";
        for (int j = 1; j < 128; ++j)
            text += " + x";
        filler.parameters.push_back(numberParameter("r" + std::to_string(i), text));
    }
    for (int i = 1; i <= 3; ++i)
        filler.parameters.push_back(numberParameter("s" + std::to_string(i), "1"));
    auto definitions = p.doc.definitions();
    definitions[p.definition] = withRecord(definitions.at(p.definition), heavy);
    definitions[second.definition] = withRecord(definitions.at(second.definition), filler);
    std::vector<ComponentOverrides> values;
    for (int k = 2; k <= 630; ++k)
        values.push_back({{"x", count(k)}});
    const auto visits = withPlacements(p.doc, p.placement, values, definitions, false);
    check(visits.componentVariants().visits == ComponentParameterLimits::documentVisits,
          "The evaluation fixture is at exactly 20,000,000 visits (got " +
              std::to_string(visits.componentVariants().visits) + ")");
    const auto visitsBytes = encodeContainer(visits);
    const auto visitsSeconds = loadSeconds(visitsBytes, visits, 1);
    // (2) Variants: 4,096 placements, each its own stretched variant.
    auto v = makePanel(Document{}, 0);
    defineComponentParameter(v.doc, v.definition,
                             parameter("w", "W", ParameterType::Length,
                                       ComponentParameterMode::Input, "1 m"));
    auto stretch = driver(ComponentDriverKind::Stretch, v.member, "w");
    stretch.at = .5;
    driveComponent(v.doc, v.definition, 0, stretch);
    values.clear();
    for (int k = 1; k <= int(ComponentParameterLimits::variants); ++k)
        values.push_back({{"w", metres(1 + .001 * k)}});
    const auto variants = withPlacements(v.doc, v.placement, values, v.doc.definitions(), true);
    check(variants.componentVariants().variants.size() == ComponentParameterLimits::variants,
          "The variant fixture has every distinct variant the limit allows (got " +
              std::to_string(variants.componentVariants().variants.size()) + ")");
    const auto variantsBytes = encodeContainer(variants);
    const auto variantsSeconds = loadSeconds(variantsBytes, variants, 1);
    std::cout << "load budget: 20,000,000-visit document " << visitsBytes.size() << " bytes "
              << visitsSeconds << " s; " << variants.componentVariants().variants.size()
              << "-variant document " << variantsBytes.size() << " bytes " << variantsSeconds
              << " s; ceiling " << loadCeilingSeconds << " s\n";
    check(visitsSeconds <= loadCeilingSeconds && variantsSeconds <= loadCeilingSeconds,
          "The worst accepted documents load within the ceiling");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 3 && args[1] == "--write-fixture") {
            auto table = makeTable();
            saveDocument(table.doc, args[2]);
            return 0;
        }
        check(args.size() == 1, "Usage: component_parameter_io_tests [--write-fixture PATH]");
        QTemporaryDir files;
        check(files.isValid(), "Temporary directory");
        const QDir fixtures(QString(SOURCE_DIR) + "/tests/fixtures");

        // The policy constants: 28 is written for parameters (29 only adds terrain and
        // the site origin, ADR 0168); parameter-free content stays 27 or 26.
        static_assert(nativeDocumentVersion == 29 && nativeTerrainFreeDocumentVersion == 28 &&
                      nativeParameterFreeDocumentVersion == 27 &&
                      nativeSheetFreeDocumentVersion == 26);
        check(nativeDocumentVersionFor(Document{}) == 26, "An empty document needs schema 26");

        auto t = makeTable();
        const auto &doc = t.doc;
        check(nativeDocumentVersionFor(doc) == 28, "Parameter records need schema 28");
        check(doc.drawingSheets().empty(), "The fixture has no drawing sheets");
        check(std::abs(maxX(doc, t.wide, t.top) - 1.2) < 1e-9 &&
                  std::abs(maxX(doc, t.nested, t.top) - 1.8) < 1e-9 &&
                  std::abs(maxX(doc, t.defaults, t.top) - .9) < 1e-9,
              "The fixture's placements show their variants");
        const auto driverFloor = doc.nextComponentDriverId();
        check(driverFloor == 9 && record(doc, t.definition).drivers.size() == 6 &&
                  record(doc, t.room).drivers.size() == 1,
              "The fixture has a retired driver ID below the allocator");
        const auto container = encodeContainer(doc);
        const auto tree = documentTree(container);
        const auto head = manifest(container);

        // ---- Shape ----
        {
            const auto features = head["requiredFeatures"].toArray();
            check(features.size() == 27 && features[25] == "drawing-sheets-v1" &&
                      features[26] == "component-parameters-v1",
                  "Schema 28 appends component-parameters-v1 after drawing-sheets-v1");
            check(head["chunks"].toArray()[0].toObject()["encoding"] == "json-v28" &&
                      tree["version"] == 28,
                  "Schema 28 chunk encoding and version");
            const auto floors = head["allocatorFloors"].toObject();
            check(floors["nextComponentDriverId"] == "9" && tree["nextComponentDriverId"] == "9",
                  "Container and root record the driver allocator");
            check(floors.contains("nextDrawingSheetId") && emptyArray(tree["drawingSheets"]) &&
                      emptyArray(tree["drawingViews"]) && emptyArray(tree["drawingItems"]),
                  "A schema-28 root always carries the (empty) schema-27 sheet tables");
            const auto tables = tree["componentParameters"].toArray();
            check(tables.size() == 2, "One record per parametric definition");
            const auto table = tables[0].toObject();
            check(keys(table) == std::set<QString>{"definition", "parameters", "drivers"} &&
                      table["definition"] == QString::number(t.definition) &&
                      tables[1].toObject()["definition"] == QString::number(t.room) &&
                      t.definition < t.room,
                  "Records are {definition, parameters, drivers} in definition order");
            const auto parameters = table["parameters"].toArray();
            QStringList names;
            for (const auto &entry : parameters)
                names.append(entry.toObject()["name"].toString());
            check(names == QStringList{"width", "shelves", "finish", "open", "tilt", "leg_x",
                                       "top_area", "ratio"},
                  "Parameters are stored in display order");
            check(parameters[0].toObject() == QJsonObject{{"name", "width"},
                                                          {"label", "Width"},
                                                          {"type", "length"},
                                                          {"mode", "input"},
                                                          {"expression", "900 mm"},
                                                          {"min", .4},
                                                          {"max", 2.4},
                                                          {"step", QJsonValue::Null},
                                                          {"options", QJsonValue::Null}},
                  "A length input stores canonical text, SI bounds and nulls");
            check(parameters[1].toObject()["type"] == "count" &&
                      parameters[1].toObject()["min"] == 0 && parameters[1].toObject()["max"] == 4 &&
                      parameters[1].toObject()["step"] == 1,
                  "A count stores its step");
            check(parameters[2].toObject()["type"] == "choice" &&
                      parameters[2].toObject()["expression"] == "\"oak\"" &&
                      parameters[2].toObject()["options"].toArray() ==
                          QJsonArray{QJsonObject{{"value", "oak"}, {"label", "Oak"}},
                                     QJsonObject{{"value", "ash"}, {"label", "Ash \"natural\""}}} &&
                      parameters[2].toObject()["min"].isNull(),
                  "A choice stores its options in order");
            check(parameters[3].toObject()["type"] == "boolean" &&
                      parameters[4].toObject()["type"] == "angle" &&
                      parameters[5].toObject()["mode"] == "derived" &&
                      parameters[5].toObject()["expression"] == "width - 60 mm" &&
                      parameters[6].toObject()["type"] == "area" &&
                      parameters[7].toObject()["type"] == "number",
                  "Every type and mode uses its documented code");
            const auto drivers = table["drivers"].toArray();
            QStringList ids, kinds;
            for (const auto &entry : drivers) {
                const auto driver = entry.toObject();
                check(keys(driver) == std::set<QString>{"id", "kind", "member", "expression",
                                                        "bound", "data"},
                      "Driver stores id, kind, member, expression, bound and data");
                check(driver["bound"].toString().size() == 64 &&
                          driver["bound"].toString() == driver["bound"].toString().toLower(),
                      "Bound is 64 lowercase hexadecimal digits");
                ids.append(driver["id"].toString());
                kinds.append(driver["kind"].toString());
            }
            check(ids == QStringList{"1", "2", "3", "4", "6", "7"},
                  "Drivers are stored in ascending ID order without the retired one");
            check(kinds == QStringList{"stretch", "position", "position", "rotation", "repeat",
                                       "visibility"},
                  "Driver kinds use their documented codes");
            check(drivers[0].toObject()["data"] == QJsonObject{{"axis", "x"}, {"at", .45}} &&
                      drivers[0].toObject()["member"] == QString::number(t.top) &&
                      drivers[0].toObject()["expression"] == "width",
                  "Stretch data");
            check(drivers[1].toObject()["data"] == QJsonObject{{"axis", "x"}} &&
                      drivers[3].toObject()["data"] == QJsonObject{{"axis", "z"}} &&
                      drivers[5].toObject()["data"] == QJsonObject{},
                  "Position, rotation and visibility data");
            const auto repeat = drivers[4].toObject();
            check(repeat["expression"] == "max(shelves, 1)" &&
                      keys(repeat["data"].toObject()) ==
                          std::set<QString>{"axis", "maxCount", "spacing", "firstMember"} &&
                      repeat["data"].toObject()["maxCount"] == 4 &&
                      repeat["data"].toObject()["spacing"] == "300 mm" &&
                      repeat["data"].toObject()["firstMember"] ==
                          QString::number(record(doc, t.definition).drivers[4].firstMember),
                  "Repeat data");
            check(tables[1].toObject()["drivers"].toArray()[0].toObject()["data"] ==
                          QJsonObject{{"parameter", "width"}} &&
                      tables[1].toObject()["drivers"].toArray()[0].toObject()["id"] == "8",
                  "childParameter data");
            const auto overrides = tree["componentParameterOverrides"].toArray();
            check(overrides.size() == 2 &&
                      overrides[0].toObject() ==
                          QJsonObject{{"instance", QString::number(t.roomPlacement)},
                                      {"values", QJsonObject{{"table_width", 1.8}}}} &&
                      overrides[1].toObject() ==
                          QJsonObject{{"instance", QString::number(t.wide)},
                                      {"values", QJsonObject{{"width", std::get<ParameterQuantity>(metres(1.2)).value},
                                                             {"shelves", 3},
                                                             {"finish", "ash"}}}} &&
                      t.roomPlacement < t.wide,
                  "Overrides are {instance, values} in placement order with SI values");
            // Bindings list the variant's members, repeat copies included; no member body
            // is stored.
            QJsonObject wide;
            for (const auto &entry : tree["instances"].toArray())
                if (entry.toObject()["root"] == QString::number(t.wide))
                    wide = entry.toObject();
            check(wide["members"].toObject().size() ==
                          qsizetype(doc.instances().at(t.wide)->members.size()) &&
                      doc.instances().at(t.wide)->members.size() ==
                          doc.instances().at(t.defaults)->members.size() + 2,
                  "The overridden placement binds its two shelf copies");
            std::set<QString> stored;
            for (const auto &entry : tree["bodies"].toArray())
                stored.insert(entry.toObject()["id"].toString());
            for (const auto &[member, target] : doc.instances().at(t.wide)->members)
                check(target == t.wide || !stored.contains(QString::number(target)),
                      "Placement members are projected, not stored");
        }

        // ---- Round trip ----
        const auto reopened = decodeContainer(container);
        sameParameters(doc, reopened, "container");
        check(encodeContainer(reopened) == container, "Container re-encodes byte-identically");
        check(reopened.bodies().size() == doc.bodies().size(), "Every projected record returns");
        for (const auto &[id, body] : doc.bodies())
            check(*reopened.bodies().at(id) == *body,
                  "Projected record " + std::to_string(id) + " equals the saved one");
        sameParameters(doc, decodeDocument(encodeDocument(doc)), "raw document");
        {
            const auto path = files.filePath("created.sketchyup");
            const auto created = createNativeFile(doc, path);
            check(created["documentVersion"] == 28 && !created["migrationNeeded"].toBool(),
                  "createNativeFile verifies schema 28");
            const auto inspected = inspectNativeFile(path);
            check(inspected["documentVersion"] == 28 && inspected["valid"] == true &&
                      inspected["componentParameters"] == 2 &&
                      inspected["componentDrivers"] == 7 &&
                      inspected["componentParameterOverrides"] == 2,
                  "Inspection reports schema 28 and the parameter counts");
            const auto plain = inspectNativeFile(fixtures.filePath("release-1.0.0-v26.sketchyup"));
            check(plain["componentParameters"] == 0 && plain["componentDrivers"] == 0 &&
                      plain["componentParameterOverrides"] == 0,
                  "Inspection reports zero counts for a parameter-free file");
        }

        // ---- Refusals: unknown, missing and noncanonical fields, stored formulas and
        // allocator disagreement. Each also leaves a file on disk byte-identical. ----
        {
            const auto raw = QJsonDocument::fromJson(encodeDocument(doc)).object();
            int fileCases{};
            auto rawRejects = [&](const std::function<void(QJsonObject &)> &mutate,
                                  const std::string &what) {
                auto changed = raw;
                mutate(changed);
                rejection([&] { (void)decodeDocument(compact(changed)); }, "raw " + what);
                auto packed = tree;
                mutate(packed);
                const auto bytes = repack(container, head, compact(packed));
                rejection([&] { (void)decodeContainer(bytes); }, "container " + what);
                if (fileCases++ % 7 == 0) {
                    const auto path = files.filePath("refused.sketchyup");
                    write(path, bytes);
                    rejection([&] { (void)loadDocument(path); }, "load " + what);
                    rejection([&] { (void)inspectNativeFile(path); }, "inspect " + what);
                    check(read(path) == bytes, what + ": the refused file is unchanged");
                }
            };
            auto onPath = [](const QStringList &path,
                             const std::function<void(QJsonObject &)> &mutate) {
                return [=](QJsonObject &root) { at(root, path, mutate); };
            };
            for (const auto &field :
                 {"componentParameters", "componentParameterOverrides", "nextComponentDriverId",
                  "drawingSheets", "nextDrawingItemId"})
                rawRejects([&](QJsonObject &root) { root.remove(field); },
                           std::string("missing root ") + field);
            rawRejects([](QJsonObject &root) { root["componentVariants"] = QJsonArray{}; },
                       "unknown root field");
            rawRejects([](QJsonObject &root) { root["componentParameters"] = QJsonObject{}; },
                       "non-array parameter table");
            // Every object in every record, by path: each field missing, and one unknown.
            const std::vector<QStringList> paths{
                {"componentParameters", "0"},
                {"componentParameters", "0", "parameters", "0"},
                {"componentParameters", "0", "parameters", "2"},
                {"componentParameters", "0", "parameters", "2", "options", "1"},
                {"componentParameters", "0", "drivers", "0"},
                {"componentParameters", "0", "drivers", "0", "data"},
                {"componentParameters", "0", "drivers", "1", "data"},
                {"componentParameters", "0", "drivers", "3", "data"},
                {"componentParameters", "0", "drivers", "4", "data"},
                {"componentParameters", "1", "drivers", "0", "data"},
                {"componentParameterOverrides", "1"},
            };
            int fieldCases{};
            for (const auto &path : paths) {
                const auto object = node(tree, path);
                check(object.isObject(), "Record path names an object");
                const auto label = path.join('/').toStdString();
                for (const auto &field : object.toObject().keys()) {
                    rawRejects(onPath(path, [&](QJsonObject &o) { o.remove(field); }),
                               "missing " + field.toStdString() + " at " + label);
                    ++fieldCases;
                }
                rawRejects(onPath(path, [](QJsonObject &o) { o["extra"] = true; }),
                           "unknown field at " + label);
            }
            rawRejects(onPath({"componentParameters", "0", "drivers", "5", "data"},
                              [](QJsonObject &o) { o["axis"] = "x"; }),
                       "a field outside the visibility kind");
            check(fieldCases >= 40, "Every stored field has a missing-field case");
            const QStringList table{"componentParameters", "0"};
            const QStringList width{"componentParameters", "0", "parameters", "0"};
            const QStringList stretch{"componentParameters", "0", "drivers", "0"};
            const QStringList repeat{"componentParameters", "0", "drivers", "4"};
            const QStringList wide{"componentParameterOverrides", "1"};
            // Identities and order.
            for (const auto &bad : {QJsonValue("02"), QJsonValue("0"), QJsonValue(1),
                                    QJsonValue("+1"), QJsonValue(" 1")}) {
                rawRejects(onPath(table, [&](QJsonObject &o) { o["definition"] = bad; }),
                           "noncanonical definition");
                rawRejects(onPath(stretch, [&](QJsonObject &o) { o["id"] = bad; }),
                           "noncanonical driver id");
                rawRejects(onPath(stretch, [&](QJsonObject &o) { o["member"] = bad; }),
                           "noncanonical driver member");
                rawRejects(onPath(wide, [&](QJsonObject &o) { o["instance"] = bad; }),
                           "noncanonical instance");
                rawRejects(onPath(repeat,
                                  [&](QJsonObject &o) {
                                      auto data = o["data"].toObject();
                                      data["firstMember"] = bad;
                                      o["data"] = data;
                                  }),
                           "noncanonical firstMember");
            }
            rawRejects([](QJsonObject &root) {
                auto tables = root["componentParameters"].toArray();
                swapFirst(tables);
                root["componentParameters"] = tables;
            }, "records out of definition order");
            rawRejects([](QJsonObject &root) {
                auto tables = root["componentParameters"].toArray();
                tables.append(tables[1]);
                root["componentParameters"] = tables;
            }, "a definition stored twice");
            rawRejects(onPath(table, [](QJsonObject &o) { o["definition"] = "999"; }),
                       "a record for a missing definition");
            rawRejects(onPath(table, [](QJsonObject &o) {
                auto drivers = o["drivers"].toArray();
                swapFirst(drivers);
                o["drivers"] = drivers;
            }), "drivers out of ID order");
            rawRejects(onPath({"componentParameters", "0", "drivers", "1"},
                              [](QJsonObject &o) { o["id"] = "1"; }),
                       "a duplicate driver ID");
            rawRejects(onPath({"componentParameters", "1", "drivers", "0"},
                              [](QJsonObject &o) { o["id"] = "1"; }),
                       "a driver ID shared by two definitions");
            rawRejects(onPath(table, [](QJsonObject &o) {
                o["parameters"] = QJsonArray{};
                o["drivers"] = QJsonArray{};
            }), "an empty record");
            rawRejects([](QJsonObject &root) {
                auto overrides = root["componentParameterOverrides"].toArray();
                swapFirst(overrides);
                root["componentParameterOverrides"] = overrides;
            }, "overrides out of placement order");
            // Enumerations and value shapes.
            rawRejects(onPath(width, [](QJsonObject &o) { o["type"] = "Length"; }),
                       "noncanonical type");
            rawRejects(onPath(width, [](QJsonObject &o) { o["type"] = "string"; }),
                       "unknown type");
            rawRejects(onPath(width, [](QJsonObject &o) { o["mode"] = "computed"; }),
                       "unknown mode");
            rawRejects(onPath(width, [](QJsonObject &o) { o["min"] = "0.4"; }),
                       "a bound stored as text");
            rawRejects(onPath(width, [](QJsonObject &o) { o["max"] = .3; }),
                       "a maximum below the minimum");
            rawRejects(onPath(width, [](QJsonObject &o) { o["options"] = QJsonArray{}; }),
                       "options on a length");
            rawRejects(onPath({"componentParameters", "0", "parameters", "3"},
                              [](QJsonObject &o) { o["min"] = 0; }),
                       "a bound on a boolean");
            rawRejects(onPath({"componentParameters", "0", "parameters", "2"},
                              [](QJsonObject &o) { o["options"] = QJsonValue::Null; }),
                       "a choice without options");
            rawRejects(onPath(width, [](QJsonObject &o) { o["name"] = "Width"; }),
                       "a malformed name");
            rawRejects(onPath(width, [](QJsonObject &o) { o["name"] = "shelves"; }),
                       "a duplicate name");
            rawRejects(onPath(width, [](QJsonObject &o) { o["name"] = "pi"; }),
                       "a reserved name");
            rawRejects(onPath(width, [](QJsonObject &o) { o["label"] = " Width"; }),
                       "a label with an edge space");
            rawRejects(onPath(stretch, [](QJsonObject &o) { o["kind"] = "Stretch"; }),
                       "noncanonical driver kind");
            rawRejects(onPath(stretch, [](QJsonObject &o) { o["kind"] = "scale"; }),
                       "unknown driver kind");
            rawRejects(onPath(stretch, [](QJsonObject &o) { o["kind"] = "position"; }),
                       "a kind that does not match its data");
            rawRejects(onPath(stretch, [](QJsonObject &o) {
                auto data = o["data"].toObject();
                data["axis"] = "w";
                o["data"] = data;
            }), "unknown axis");
            rawRejects(onPath(repeat, [](QJsonObject &o) {
                auto data = o["data"].toObject();
                data["maxCount"] = 1001;
                o["data"] = data;
            }), "a repeat above 1,000");
            rawRejects(onPath(repeat, [](QJsonObject &o) {
                auto data = o["data"].toObject();
                data["maxCount"] = 2.5;
                o["data"] = data;
            }), "a fractional maxCount");
            for (const auto &bound : {QString(63, 'a'), QString(64, 'A'), QString(64, 'g')})
                rawRejects(onPath(stretch, [&](QJsonObject &o) { o["bound"] = bound; }),
                           "a malformed bound");
            rawRejects(onPath(stretch, [](QJsonObject &o) { o["member"] = "999"; }),
                       "a driver whose target member is missing");
            // Stored formulas are checked again on load (ADR 0165 §3).
            rawRejects(onPath(width, [](QJsonObject &o) { o["expression"] = "900mm"; }),
                       "a noncanonical expression");
            rawRejects(onPath(width, [](QJsonObject &o) { o["expression"] = "900 mm "; }),
                       "an expression with trailing space");
            rawRejects(onPath({"componentParameters", "0", "parameters", "5"},
                              [](QJsonObject &o) { o["expression"] = "leg_x - 60 mm"; }),
                       "a self-referencing (cyclic) formula");
            rawRejects([](QJsonObject &root) {
                at(root, {"componentParameters", "0", "parameters", "6"},
                   [](QJsonObject &o) { o["expression"] = "ratio * 1 m ^ 2"; });
                at(root, {"componentParameters", "0", "parameters", "7"},
                   [](QJsonObject &o) { o["expression"] = "top_area / 1 m ^ 2"; });
            }, "a cycle through two derived parameters");
            rawRejects(onPath({"componentParameters", "0", "parameters", "5"},
                              [](QJsonObject &o) { o["expression"] = "depth - 60 mm"; }),
                       "an unknown name");
            rawRejects(onPath({"componentParameters", "0", "parameters", "5"},
                              [](QJsonObject &o) { o["expression"] = "width > 1 m"; }),
                       "an ill-typed formula");
            rawRejects(onPath(stretch, [](QJsonObject &o) { o["expression"] = "shelves"; }),
                       "an ill-typed driver expression");
            rawRejects(onPath(repeat, [](QJsonObject &o) {
                auto data = o["data"].toObject();
                data["spacing"] = "300mm";
                o["data"] = data;
            }), "a noncanonical spacing");
            rawRejects(onPath(width, [](QJsonObject &o) { o["expression"] = "3 m"; }),
                       "a default outside its bounds");
            // Overrides.
            auto onValues = [&](const std::function<void(QJsonObject &)> &mutate) {
                return onPath(wide, [=](QJsonObject &o) {
                    auto values = o["values"].toObject();
                    mutate(values);
                    o["values"] = values;
                });
            };
            rawRejects(onValues([](QJsonObject &v) { v["depth"] = 1; }), "an unknown parameter");
            rawRejects(onValues([](QJsonObject &v) { v["leg_x"] = 1; }),
                       "an override of a derived parameter");
            rawRejects(onValues([](QJsonObject &v) { v["width"] = "1200 mm"; }),
                       "a length stored as text");
            rawRejects(onValues([](QJsonObject &v) { v["width"] = 1.2000000000001; }),
                       "a value that is not quantized");
            rawRejects(onValues([](QJsonObject &v) { v["width"] = 3; }),
                       "a value outside its bounds");
            rawRejects(onValues([](QJsonObject &v) { v["shelves"] = 2.5; }),
                       "a fractional count");
            rawRejects(onValues([](QJsonObject &v) { v["finish"] = "teak"; }),
                       "an unknown choice");
            rawRejects(onValues([](QJsonObject &v) { v["open"] = 1; }),
                       "a boolean stored as a number");
            rawRejects(onPath(wide, [](QJsonObject &o) { o["values"] = QJsonObject{}; }),
                       "an empty override set");
            rawRejects(onPath(wide, [](QJsonObject &o) { o["values"] = QJsonArray{}; }),
                       "override values that are not an object");
            rawRejects(onPath(wide, [](QJsonObject &o) { o["instance"] = "999"; }),
                       "overrides of a missing placement");
            rawRejects(onPath(wide, [&](QJsonObject &o) {
                o["instance"] = QString::number(t.nested);
            }), "overrides of a nested placement");
            rawRejects([&](QJsonObject &root) {
                auto overrides = root["componentParameterOverrides"].toArray();
                overrides.append(QJsonObject{{"instance", QString::number(t.roomPlacement + 0)},
                                             {"values", QJsonObject{{"table_width", 1}}}});
                root["componentParameterOverrides"] = overrides;
            }, "a placement stored twice");
            // A placement whose definition loses its parameters keeps no overrides.
            rawRejects([](QJsonObject &root) {
                auto tables = root["componentParameters"].toArray();
                tables.removeAt(1);
                root["componentParameters"] = tables;
            }, "overrides of a definition without parameters");
            // Bindings must list exactly the variant's members.
            rawRejects([&](QJsonObject &root) {
                auto instances = root["instances"].toArray();
                for (qsizetype i = 0; i < instances.size(); ++i) {
                    auto instance = instances[i].toObject();
                    if (instance["root"] != QString::number(t.wide))
                        continue;
                    auto members = instance["members"].toObject();
                    members.remove(members.keys().last());
                    instance["members"] = members;
                    instances[i] = instance;
                }
                root["instances"] = instances;
            }, "a binding without a repeat copy");
            rawRejects(onValues([](QJsonObject &v) { v["shelves"] = 1; }),
                       "bindings that list copies the values do not make");
            // Allocator disagreement.
            rawRejects([](QJsonObject &root) { root["nextComponentDriverId"] = "8"; },
                       "a driver allocator at a live driver ID");
            rawRejects([](QJsonObject &root) { root["nextComponentDriverId"] = "0"; },
                       "a zero driver allocator");
            {
                auto changed = head;
                auto floors = changed["allocatorFloors"].toObject();
                floors["nextComponentDriverId"] = "10";
                changed["allocatorFloors"] = floors;
                rejection([&] { (void)decodeContainer(repack(container, changed, documentChunk(container))); },
                          "manifest driver floor disagreeing with the root");
                floors.remove("nextComponentDriverId");
                changed["allocatorFloors"] = floors;
                rejection([&] { (void)decodeContainer(repack(container, changed, documentChunk(container))); },
                          "manifest without the driver floor");
                // Raising the allocator consistently in both places is valid.
                auto raised = tree;
                raised["nextComponentDriverId"] = "40";
                floors["nextComponentDriverId"] = "40";
                changed["allocatorFloors"] = floors;
                const auto valid =
                    decodeContainer(repack(container, changed, compact(raised)));
                check(valid.nextComponentDriverId() == 40, "A raised allocator is kept");
            }
            // The feature, the encoding and the payload version must agree.
            {
                auto older = head;
                auto features = older["requiredFeatures"].toArray();
                features.removeLast();
                older["requiredFeatures"] = features;
                rejection([&] { (void)decodeContainer(repack(container, older, documentChunk(container))); },
                          "a v28 chunk without component-parameters-v1");
                auto chunk = tree;
                chunk["version"] = 27;
                rejection([&] { (void)decodeContainer(repack(container, head, compact(chunk))); },
                          "component-parameters-v1 with a v27 payload");
                auto encoded = head;
                auto chunks = encoded["chunks"].toArray();
                auto first = chunks[0].toObject();
                first["encoding"] = "json-v27";
                chunks[0] = first;
                encoded["chunks"] = chunks;
                rejection([&] { (void)decodeContainer(repack(container, encoded, documentChunk(container))); },
                          "component-parameters-v1 with a json-v27 chunk");
                auto repeated = head;
                features = repeated["requiredFeatures"].toArray();
                features.append("component-parameters-v1");
                repeated["requiredFeatures"] = features;
                rejection([&] { (void)decodeContainer(repack(container, repeated, documentChunk(container))); },
                          "a repeated component-parameters-v1");
                // Schema 27 does not know the tables.
                auto v27 = QJsonDocument::fromJson(encodeDocument(Document{})).object();
                v27["version"] = 27;
                for (const auto &field : {"drawingSheets", "drawingViews", "drawingItems"})
                    v27[field] = QJsonArray{};
                for (const auto &field :
                     {"nextDrawingSheetId", "nextDrawingViewId", "nextDrawingItemId"})
                    v27[field] = "1";
                (void)decodeDocument(compact(v27));
                v27["componentParameters"] = QJsonArray{};
                rejection([&] { (void)decodeDocument(compact(v27)); },
                          "component parameter tables in a schema-27 document");
            }
            std::cout << fileCases << " refusal cases\n";
        }

        // ---- Writer policy: 26, 27 and 28 and back ----
        {
            const auto releasePath = fixtures.filePath("release-1.0.0-v26.sketchyup");
            auto plain = loadDocument(releasePath);
            check(plain.nextComponentDriverId() == 1 && plain.componentVariants().placements.empty(),
                  "A 1.0.0 document loads with an empty driver allocator");
            const auto path = files.filePath("policy.sketchyup");
            saveDocument(plain, path);
            check(documentVersion(read(path)) == 26, "A plain document writes 26");
            const auto body = plain.bodies().begin()->first;
            const auto component = createComponent(plain, body, "Box");
            saveDocument(plain, path);
            check(documentVersion(read(path)) == 26, "A plain component writes 26");
            // Parameters without sheets write 28 with empty sheet tables.
            defineComponentParameter(plain, component.definition,
                                     parameter("size", "Size", ParameterType::Length,
                                               ComponentParameterMode::Input, "1 m"));
            saveDocument(plain, path);
            auto saved = read(path);
            check(documentVersion(saved) == 28 &&
                      emptyArray(documentTree(saved)["drawingSheets"]) &&
                      manifest(saved)["requiredFeatures"].toArray().last() ==
                          "component-parameters-v1",
                  "A parameter record writes 28");
            const auto driven = driveComponent(plain, component.definition, 0,
                                               driver(ComponentDriverKind::Visibility,
                                                      plain.definitions()
                                                          .at(component.definition)
                                                          ->members.rbegin()
                                                          ->first,
                                                      "size > 0 m"))
                                    .createdDrivers.at(0);
            check(driven == 1, "The first driver takes ID 1");
            const auto sheet = createDrawingSheet(plain, "Sheet 1", {420, 297});
            saveDocument(plain, path);
            check(documentVersion(read(path)) == 28 &&
                      documentTree(read(path))["drawingSheets"].toArray().size() == 1,
                  "Sheets and parameters write 28 with the sheets");
            check(documentVersion(read(path + ".bak")) == 28, "The backup is the previous 28");
            // Deleting every parameter record writes 27 again; the allocator is not stored
            // and restarts at 1 on reopen.
            plain.undo(); // the sheet
            plain.undo(); // the driver
            plain.undo(); // the parameter
            check(!plain.definitions().at(component.definition)->parameters &&
                      plain.nextComponentDriverId() == 2,
                  "Undo removes the record; the allocator never rewinds");
            createDrawingSheet(plain, "Sheet 1", {420, 297});
            saveDocument(plain, path);
            check(documentVersion(read(path)) == 27 &&
                      !documentTree(read(path)).contains("nextComponentDriverId") &&
                      !manifest(read(path))["allocatorFloors"].toObject().contains(
                          "nextComponentDriverId") &&
                      manifest(read(path))["requiredFeatures"].toArray().last() ==
                          "drawing-sheets-v1",
                  "Deleting every parameter record writes 27");
            check(loadDocument(path).nextComponentDriverId() == 1,
                  "Schema 27 does not store the driver allocator");
            check(documentVersion(read(path + ".bak")) == 28,
                  "The previous schema-28 save is kept as the backup");
            eraseDrawingSheet(plain, plain.drawingSheets().begin()->first);
            saveDocument(plain, path);
            check(documentVersion(read(path)) == 26, "Deleting the sheet too writes 26");
            (void)sheet;
            // Overrides alone cannot exist without parameters, so 28 follows records.
            check(nativeDocumentVersionFor(t.doc) == 28, "The fixture stays 28");
            // Removing the parameters of a parametric document through history.
            auto table = makeTable();
            while (nativeDocumentVersionFor(table.doc) == 28)
                table.doc.undo();
            check(nativeDocumentVersionFor(table.doc) == 26 &&
                      table.doc.nextComponentDriverId() == 9,
                  "Undoing every parameter edit returns to 26 with the allocator kept");
        }

        // ---- Save, backup and recovery ----
        {
            auto edited = makeTable();
            const auto path = files.filePath("saved.sketchyup");
            saveDocument(edited.doc, path);
            check(!edited.doc.dirty(), "Save marks the document clean");
            sameParameters(edited.doc, loadDocument(path), "save");
            const auto first = read(path);
            overrideComponentParameters(edited.doc, edited.wide, {{"shelves", count(4)}});
            saveDocument(edited.doc, path);
            check(read(path + ".bak") == first, "Backup keeps the previous container");
            check(loadDocument(path + ".bak").instances().at(edited.wide)->overrides.at("shelves") ==
                      count(3),
                  "The backup has the previous override");
            const auto saved = loadDocument(path);
            sameParameters(edited.doc, saved, "second save");
            check(saved.instances().at(edited.wide)->members.size() ==
                      saved.instances().at(edited.defaults)->members.size() + 3,
                  "The saved placement binds three shelf copies");
            const auto snapshot = captureSave(edited.doc);
            resetComponentParameters(edited.doc, edited.wide);
            saveSnapshot(edited.doc, snapshot, path);
            check(loadDocument(path).instances().at(edited.wide)->overrides.size() == 3 &&
                      edited.doc.instances().at(edited.wide)->overrides.empty() &&
                      edited.doc.dirty(),
                  "A captured snapshot saves the captured overrides, not later edits");
            const auto root = files.filePath("recovery");
            QString key;
            {
                RecoveryWriter writer(root, QString::fromStdString(edited.doc.identity()));
                writer.write(captureRecovery(edited.doc, {path, std::nullopt, {}}));
                key = writer.key();
            }
            const auto recovered = readRecovery(root, key);
            check(recovered.verified && recovered.document.has_value(),
                  "Recovery journal verifies: " + recovered.issue.toStdString());
            sameParameters(edited.doc, *recovered.document, "recovery");
            check(recovered.document->instances().at(edited.wide)->overrides.empty(),
                  "Recovery keeps the unsaved reset");
        }

        // ---- Component bundles (ADR 0130) and templates ----
        {
            const auto png = encodeTexturePng(TextureImage(1, 1, {10, 20, 30, 255}));
            const TemplateMetadata metadata{"Room", "Parametric room", {"test"}, 0};
            const auto bundleBytes = encodeComponentBundle(doc, t.room, metadata, png);
            const auto bundle = decodeComponentBundle(bundleBytes);
            const auto &captured = bundle.document;
            check(captured.definitions().size() == 2 &&
                      captured.definitions().at(t.room)->parameters &&
                      captured.definitions().at(t.definition)->parameters &&
                      *captured.definitions().at(t.definition)->parameters ==
                          record(doc, t.definition) &&
                      captured.nextComponentDriverId() == driverFloor,
                  "A bundle captures every captured definition's parameter record");
            Id bundleRoot{};
            for (const auto &[id, body] : captured.bodies())
                if (!body->parent)
                    bundleRoot = id;
            check(captured.instances().at(bundleRoot)->overrides.empty() &&
                      nativeDocumentVersionFor(captured) == 28,
                  "The bundle's root placement has no overrides and the model is schema 28");
            // A root placement with overrides is refused.
            {
                auto withOverrides = captured.readSnapshot();
                overrideComponentParameters(withOverrides, bundleRoot,
                                            {{"table_width", metres(2)}});
                const auto model = encodeContainer(withOverrides);
                auto forged = bundleBytes;
                const auto manifestSize = qFromLittleEndian<quint32>(forged.constData() + 8);
                const auto modelSize = qFromLittleEndian<quint64>(forged.constData() + 12);
                auto head = QJsonDocument::fromJson(forged.mid(24, manifestSize)).object();
                auto modelInfo = head["model"].toObject();
                modelInfo["bytes"] = model.size();
                modelInfo["sha256"] = sha256(model);
                head["model"] = modelInfo;
                const auto encodedHead = QJsonDocument(head).toJson(QJsonDocument::Compact);
                const auto thumbnail = forged.mid(24 + manifestSize + qsizetype(modelSize));
                forged.resize(24);
                qToLittleEndian<quint32>(quint32(encodedHead.size()), forged.data() + 8);
                qToLittleEndian<quint64>(quint64(model.size()), forged.data() + 12);
                forged += encodedHead + model + thumbnail;
                rejection([&] { (void)decodeComponentBundle(forged); },
                          "a bundle whose root placement has overrides");
            }
            // Insertion allocates new driver IDs above the destination's allocator, keeps
            // members, repeat blocks and child parameters, and keeps bound drivers bound.
            auto destination = makeTable();
            const auto before = destination.doc.nextComponentDriverId();
            const auto definitionsBefore = destination.doc.definitions();
            const auto inserted = insertLibraryComponent(destination.doc, bundle,
                                                         Transform::translation({8, 0, 0}));
            const auto &after = destination.doc;
            std::vector<Id> newDefinitions;
            for (const auto &[id, definition] : after.definitions())
                if (!definitionsBefore.contains(id))
                    newDefinitions.push_back(id);
            check(newDefinitions.size() == 2, "Insertion adds both definitions");
            std::set<Id> driverIds;
            for (const auto id : newDefinitions) {
                const auto &definition = *after.definitions().at(id);
                check(bool(definition.parameters), "Inserted definitions keep their parameters");
                const auto source = definition.name.starts_with("Room") ? t.room : t.definition;
                const auto &original = record(captured, source);
                check(definition.parameters->parameters == original.parameters &&
                          definition.parameters->drivers.size() == original.drivers.size(),
                      "Inserted parameter lists are unchanged");
                for (std::size_t i = 0; i < original.drivers.size(); ++i) {
                    const auto &copy = definition.parameters->drivers[i];
                    const auto &from = original.drivers[i];
                    check(copy.id >= before && driverIds.insert(copy.id).second &&
                              (i == 0 || copy.id > definition.parameters->drivers[i - 1].id),
                          "Inserted drivers take new ascending IDs above the allocator");
                    check(copy.kind == from.kind && copy.member == from.member &&
                              copy.firstMember == from.firstMember &&
                              copy.parameter == from.parameter && copy.expression == from.expression,
                          "Inserted drivers keep members, repeat blocks and child parameters");
                    check(componentDriverState(definition, after.definitions(), copy) ==
                              ComponentDriverState::Bound,
                          "A bound driver stays bound after insertion");
                }
            }
            check(after.nextComponentDriverId() == before + 7,
                  "The allocator advances past the inserted drivers");
            check(after.instances().at(inserted.component.instance)->overrides.empty(),
                  "The inserted placement starts at its defaults");
            const auto reopenedDestination = decodeContainer(encodeContainer(after));
            sameParameters(after, reopenedDestination, "inserted bundle save");
            destination.doc.undo();
            check(destination.doc.nextComponentDriverId() == before + 7 &&
                      destination.doc.definitions().size() == definitionsBefore.size(),
                  "Undo removes the insertion; the allocator never rewinds");

            // Templates keep parameter records and overrides.
            const TemplateMetadata templateMetadata{"Tables", "Parametric tables", {"test"}, 0};
            const auto templateBytes = encodeTemplateBundle(doc, templateMetadata, png);
            const auto templateBundle = decodeTemplateBundle(templateBytes);
            sameParameters(doc, templateBundle.document, "template bundle");
            const auto instance = instantiateTemplate(templateBundle);
            sameParameters(doc, instance, "instantiated template");
            check(instance.identity() != doc.identity() && instance.revision() == 0 &&
                      std::abs(maxX(instance, t.wide, t.top) - 1.2) < 1e-9,
                  "An instantiated template has a fresh identity and the stored variants");
            check(nativeDocumentVersionFor(instance) == 28, "A parametric template is schema 28");
        }

        // ---- Historical fixtures: empty tables, unchanged bytes ----
        {
            const auto names =
                fixtures.entryList({"*.sketchyup", "raw-*.json"}, QDir::Files, QDir::Name);
            int historical{}, exact{};
            for (const auto &name : names) {
                if (name == "component-parameters-v28.sketchyup")
                    continue;
                const auto source = fixtures.filePath(name);
                const auto original = read(source);
                const auto loaded = loadDocument(source);
                bool parametric = false;
                for (const auto &[id, definition] : loaded.definitions())
                    parametric = parametric || definition->parameters;
                for (const auto &[root, instance] : loaded.instances())
                    parametric = parametric || !instance->overrides.empty();
                check(!parametric && loaded.nextComponentDriverId() == 1 &&
                          loaded.componentVariants().placements.empty(),
                      name.toStdString() + " loads with empty parameter tables");
                const auto encoded = encodeContainer(loaded);
                check(documentVersion(encoded) <= 27, name.toStdString() + " stays at most 27");
                const bool container = original.startsWith(QByteArray("SKUPDOC\0", 8));
                if (container && documentVersion(original) >= 26) {
                    // Current-shape files re-save byte-identically apart from the writer.
                    check(documentChunk(encoded) == documentChunk(original),
                          name.toStdString() + " re-encodes its document chunk exactly");
                    auto head = manifest(encoded), originalHead = manifest(original);
                    head.remove("writer");
                    originalHead.remove("writer");
                    check(head == originalHead, name.toStdString() + " manifest re-encodes exactly");
                    ++exact;
                }
                const auto output = files.filePath("migrated-" + name + ".sketchyup");
                const auto report = migrateNativeFile(source, output);
                check(report["outputDocumentVersion"].toInt() == documentVersion(read(output)) &&
                          read(source) == original,
                      name.toStdString() + " migrates without changing the source");
                ++historical;
            }
            check(historical >= 31 && exact == 3,
                  "Historical fixtures, including release-1.0.0, drawing-sheets and release-1.1.0");
            std::cout << historical << " historical fixtures load with empty parameter tables\n";
            // The retained 1.1.0 output re-saves exactly as 1.1.0 wrote it.
            const auto release = read(fixtures.filePath("release-1.1.0-v27.sketchyup"));
            check(manifest(release)["writer"] == "SketchyUp/1.1.0" &&
                      encodeContainer(decodeContainer(release)).mid(16).size() > 0,
                  "The 1.1.0 fixture is the released writer's output");
            const auto resaved = encodeContainer(decodeContainer(release));
            auto head = manifest(resaved), releaseHead = manifest(release);
            check(head["writer"] == SKETCHYUP_WRITER, "Only the writer string names this build");
            head.remove("writer");
            releaseHead.remove("writer");
            check(head == releaseHead && documentChunk(resaved) == documentChunk(release),
                  "A parameter-free save keeps the exact 1.1.0 schema-27 shape");
        }

        // ---- The retained writer fixture ----
        {
            const auto path = fixtures.filePath("component-parameters-v28.sketchyup");
            const auto original = read(path);
            const auto loaded = loadDocument(path);
            const auto expected = makeTable();
            check(encodeComponentParameters(loaded.definitions()) ==
                          encodeComponentParameters(expected.doc.definitions()) &&
                      encodeComponentParameterOverrides(loaded.instances()) ==
                          encodeComponentParameterOverrides(expected.doc.instances()) &&
                      loaded.nextComponentDriverId() == expected.doc.nextComponentDriverId(),
                  "The retained v28 fixture holds the table fixture's records");
            for (const auto &[id, body] : expected.doc.bodies())
                check(loaded.bodies().contains(id) && *loaded.bodies().at(id) == *body,
                      "The retained fixture projects the same records");
            const auto encoded = encodeContainer(loaded);
            check(documentChunk(encoded) == documentChunk(original),
                  "Retained v28 fixture re-encodes its document chunk exactly");
            auto head = manifest(encoded), originalHead = manifest(original);
            head.remove("writer");
            originalHead.remove("writer");
            check(head == originalHead, "Retained v28 manifest re-encodes exactly");
            const auto report = migrateNativeFile(path, files.filePath("fixture28.sketchyup"));
            check(report["outputDocumentVersion"] == 28 && !report["migrationNeeded"].toBool() &&
                      read(path) == original,
                  "The v28 fixture is current and unchanged");

            // D-038: the stored digests are the 1.2.0 ones; opening reports no driver
            // changed, and save, reopen and save again keeps the bytes.
            auto allBound = [](const Document &doc) {
                std::size_t drivers{};
                for (const auto &[id, definition] : doc.definitions())
                    if (definition->parameters)
                        for (const auto &driver : definition->parameters->drivers) {
                            ++drivers;
                            if (componentDriverState(*definition, doc.definitions(), driver) !=
                                ComponentDriverState::Bound)
                                return std::size_t{};
                        }
                return drivers;
            };
            check(allBound(loaded) > 0, "The retained v28 fixture opens with every driver bound");
            auto resaved = loadDocument(path);
            const auto first = files.filePath("fixture28-first.sketchyup");
            const auto second = files.filePath("fixture28-second.sketchyup");
            saveDocument(resaved, first);
            auto reopened = loadDocument(first);
            check(allBound(reopened) > 0, "A re-saved v28 fixture reopens with every driver bound");
            saveDocument(reopened, second);
            check(read(first) == read(second) && documentChunk(read(first)) == documentChunk(original),
                  "Save, reopen and save again keeps the v28 bytes");
            // A paint on a driven member keeps its driver bound across save and reopen.
            auto painted = loadDocument(path);
            const auto paint = editComponentDefinition(
                painted, expected.definition, [&](Document &draft) {
                    draft.paint(expected.top, {.4f, .25f, .1f});
                    return ChangeReport{};
                });
            check(paint.parameterWarnings.empty() && allBound(painted) > 0,
                  "Painting the fixture's stretched top leaves its drivers bound");
            const auto paintedPath = files.filePath("fixture28-painted.sketchyup");
            saveDocument(painted, paintedPath);
            auto paintedAgain = loadDocument(paintedPath);
            check(allBound(paintedAgain) > 0 &&
                      paintedAgain.definitions().at(expected.definition)->members.at(
                          expected.top)->color == std::array<float, 3>{.4f, .25f, .1f},
                  "The painted fixture reopens painted with every driver bound");
            const auto paintedSecond = files.filePath("fixture28-painted-second.sketchyup");
            saveDocument(paintedAgain, paintedSecond);
            check(read(paintedPath) == read(paintedSecond),
                  "Save, reopen and save again keeps the painted v28 bytes");
        }

        // ---- The previous-release window (ADR 0165 §5) ----
        // SKETCHYUP_PRIOR_CLI names a released sketchyup-cli (package acceptance passes
        // the previous release's: 1.1.1-1 for 1.2.0, 1.2.0-1 for 1.3.0; R093.c also ran
        // 1.0.1-1's and 1.1.0-1's). A prior that reads schema 26 or 27 must refuse the
        // schema-28 fixture with its newer-format error and leave it unchanged; a 1.2.x
        // prior reads schema 28 and must open the fixture and a schema-28 save from this
        // build unchanged (terrain_io checks its schema-29 refusal). Either way it opens a
        // parameter-free save from this build at the newest schema it reads.
        const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
        if (!prior.isEmpty()) {
            QByteArray out, err;
            check(runPriorCli(prior, {"--format-capabilities"}, out, err) == 0,
                  "The prior release describes its formats");
            int reads{};
            for (const auto &value :
                 QJsonDocument::fromJson(out).object()["readDocumentVersions"].toArray())
                reads = std::max(reads, value.toInt());
            check(reads >= 26 && reads <= 28, "The prior release reads schema 26, 27 or 28");
            const auto fixturePath = files.filePath("prior-v28.sketchyup");
            const auto fixtureBytes = read(fixtures.filePath("component-parameters-v28.sketchyup"));
            write(fixturePath, fixtureBytes);
            const auto savedPath = files.filePath("prior-v28-save.sketchyup");
            const auto savedBytes = encodeContainer(t.doc);
            write(savedPath, savedBytes);
            if (reads >= 28) {
                check(runPriorCli(prior, {"--validate-native", fixturePath}, out, err) == 0 &&
                          QJsonDocument::fromJson(out).object()["valid"] == true &&
                          read(fixturePath) == fixtureBytes,
                      "The prior release opens the schema-28 fixture unchanged: " +
                          err.toStdString());
                check(runPriorCli(prior, {"--validate-native", savedPath}, out, err) == 0 &&
                          QJsonDocument::fromJson(out).object()["valid"] == true &&
                          read(savedPath) == savedBytes,
                      "The prior release opens a schema-28 save unchanged: " +
                          err.toStdString());
            } else {
                check(runPriorCli(prior, {"--validate-native", fixturePath}, out, err) != 0 &&
                          err.contains("\"status\":\"failed\"") &&
                          read(fixturePath) == fixtureBytes,
                      "The prior release refuses the schema-28 fixture and leaves it unchanged");
                std::cout << "prior release refusal: " << err.trimmed().toStdString() << '\n';
                check(runPriorCli(prior, {"--validate-native", savedPath}, out, err) != 0 &&
                          read(savedPath) == savedBytes,
                      "The prior release refuses a schema-28 save unchanged");
            }
            // A parameter-free save: the table with every parameter edit undone, saved by
            // this build at schema 26; with sheets at 27 when the prior reads 27.
            auto plain = makeTable();
            while (nativeDocumentVersionFor(plain.doc) == 28)
                plain.doc.undo();
            const auto freePath = files.filePath("prior-free.sketchyup");
            saveDocument(plain.doc, freePath);
            check(documentVersion(read(freePath)) == 26 &&
                      runPriorCli(prior, {"--validate-native", freePath}, out, err) == 0 &&
                      QJsonDocument::fromJson(out).object()["valid"] == true,
                  "The prior release opens a parameter-free schema-26 save: " +
                      err.toStdString());
            if (reads >= 27) {
                createDrawingSheet(plain.doc, "Sheet 1", {420, 297});
                saveDocument(plain.doc, freePath);
                check(documentVersion(read(freePath)) == 27 &&
                          runPriorCli(prior, {"--validate-native", freePath}, out, err) == 0,
                      "The prior release opens a parameter-free schema-27 save");
            }
            std::cout << "prior release CLI checked: " << prior.toStdString() << " (reads up to "
                      << reads << ")\n";
        }
        loadBudget();
        std::cout << checks << " checks; component parameter persistence passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
