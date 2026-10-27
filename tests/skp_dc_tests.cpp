// R104.a and R104.c (ADR 0178, criteria D1.1 to D1.9): the Dynamic Component metadata of a SketchUp
// file, through the real path (the helper, then the import). The cases run on the self-authored
// fixtures tests/fixtures/skp/dc_*.skp (written by generate_dc.cpp), and on package edits the
// writer cannot make: an over-cap string, and a value the package carries without a value. With
// --real and SKETCHYUP_SKP_CORPUS (the directory that holds BanyanHarbor.skp, as the other skp
// tests read it), the owner's file is imported too, and only its aggregate counts are printed (ADR
// 0178 D1.8).
//
// D1.9 has two parts. The source scan always runs: no socket, network or HTTP code in src/skp/ or
// src/io/skp_*. The namespace run runs the helper under `unshare -rn` (its own network namespace,
// with no network) when the runner allows it, and exits 77 (skipped) when it does not.

#include "core/component_records.hpp"
#include "core/model.hpp"
#include "io/skp_import.hpp"
#include "io/skp_package.hpp"
#include "io/skp_worker.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace sketchy;

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

// The Dynamic Component key's metre value, within a nanometre-scale tolerance (ADR 0178 D1.7: 1e-12
// m).
bool metres(double value, double inches) { return std::abs(value - inches * 0.0254) <= 1e-12; }

QString fixture(const char *name) {
    return QStringLiteral(SOURCE_DIR "/tests/fixtures/skp/") + QString::fromLatin1(name);
}

SkpWorkerOptions workerOptions() {
    SkpWorkerOptions options;
    options.workerPath = QStringLiteral(SKP_WORKER_PATH);
    options.scratchRoot = QDir::tempPath();
    return options;
}

// The package the helper writes for a file, decoded with the checked decoder: the bytes an import
// reads.
SkpPackage packageOf(const QString &file, const QTemporaryDir &temp) {
    const auto out = temp.path() + "/" + QFileInfo(file).completeBaseName() + ".skskpp";
    QProcess process;
    process.start(QStringLiteral(SKP_WORKER_PATH), {"--import", file, "--output", out});
    check(process.waitForFinished(120000) && process.exitCode() == 0,
          "the helper's package of " + file.toStdString());
    QFile packageFile(out);
    check(packageFile.open(QIODevice::ReadOnly), "read the package of " + file.toStdString());
    const auto bytes = packageFile.readAll();
    return decodeSkpPackage(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

// The whole import of a file, through the application's runner. A refusal is a failure of the test.
SkpImport importOf(const QString &file) {
    try {
        return importSkpFile(file, workerOptions());
    } catch (const IfcImportError &error) {
        check(false, "the import of " + file.toStdString() + " was refused: " + error.code() +
                         ": " + error.what());
        throw;
    }
}

using Value = std::variant<bool, double, std::string>;

std::optional<Value> propertyOf(const Body *body, const std::string &key) {
    if (!body)
        return std::nullopt;
    const auto found = body->properties.find(key);
    if (found == body->properties.end())
        return std::nullopt;
    return found->second;
}

bool numberIs(const Body *body, const std::string &key, double expected, double tolerance = 1e-12) {
    const auto value = propertyOf(body, key);
    return value && std::holds_alternative<double>(*value) &&
           std::abs(std::get<double>(*value) - expected) <= tolerance;
}

bool textIs(const Body *body, const std::string &key, const std::string &expected) {
    const auto value = propertyOf(body, key);
    return value && std::holds_alternative<std::string>(*value) &&
           std::get<std::string>(*value) == expected;
}

bool textLength(const Body *body, const std::string &key, std::size_t bytes) {
    const auto value = propertyOf(body, key);
    return value && std::holds_alternative<std::string>(*value) &&
           std::get<std::string>(*value).size() == bytes;
}

// The top-level group named `name`: a placement of the root.
const Body *placementNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        if (body->kind == BodyKind::Group && body->name == name)
            return body.get();
    }
    return nullptr;
}

// A placement nested in a definition: the group named `name` among the members of any definition.
const Body *nestedNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, definition] : doc.definitions()) {
        (void)id;
        for (const auto &[member, body] : definition->members) {
            (void)member;
            if (body->kind == BodyKind::Group && body->name == name)
                return body.get();
        }
    }
    return nullptr;
}

// The faces the document holds, over every body. A placed component contributes its faces once per
// placement.
long long facesIn(const Document &doc) {
    long long faces = 0;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        faces += static_cast<long long>(body->surface.faces.size());
    }
    return faces;
}

// No component definition holds a parameter record (ADR 0178 D1.3: a formula is text, never a
// record).
bool noParameterRecords(const Document &doc) {
    for (const auto &[id, definition] : doc.definitions()) {
        (void)id;
        if (definition->parameters)
            return false;
    }
    return true;
}

long long count(const QJsonObject &object, const char *key) {
    return object[key].toVariant().toLongLong();
}

// The Dynamic Component section (ADR 0178 section 6): the counts the report holds.
QJsonObject dynamicOf(const SkpImport &imported) {
    return imported.report["dynamicComponents"].toObject();
}

// The accounting of one level: counted = imported + omitted (ADR 0178 D1.2).
bool levelBalances(const QJsonObject &dynamic, const char *level) {
    const QJsonObject item = dynamic["accounting"].toObject()[level].toObject();
    return item["counted"].toVariant().toLongLong() ==
           item["imported"].toVariant().toLongLong() + item["omitted"].toVariant().toLongLong();
}

// (a) instance-level values of every kind, and the Length in metres (D1.1, D1.2, D1.7).
void instanceKinds(const QTemporaryDir &temp) {
    auto imported = importOf(fixture("dc_instance_kinds.skp"));
    const Body *panel = placementNamed(imported.document, "Panel A");
    check(panel != nullptr, "instance kinds: the placement Panel A is imported");
    check(textIs(panel, "skp.dynamic_attributes.Label", "Oak panel"),
          "instance kinds: the string Label is kept");
    check(numberIs(panel, "skp.dynamic_attributes.Qty", 3.0),
          "instance kinds: the integer Qty is 3");
    check(numberIs(panel, "skp.dynamic_attributes.Weight", 1.5),
          "instance kinds: the float Weight is 1.5");
    const auto flag = propertyOf(panel, "skp.dynamic_attributes.Flag");
    check(flag && std::holds_alternative<bool>(*flag) && std::get<bool>(*flag),
          "instance kinds: the boolean Flag is true");
    check(panel && numberIs(panel, "skp.dynamic_attributes.LenX", 12.0 * 0.0254, 1e-12),
          "instance kinds: a 12-inch Length is 0.3048 m (D1.7)");
    check(panel && numberIs(panel, "skp.dynamic_attributes.Unit", 1.0 * 0.0254, 1e-12),
          "instance kinds: a one-inch Length is 0.0254 m to within 1e-12 (D1.7)");
    const auto dynamic = dynamicOf(imported);
    check(count(dynamic, "metadataImported") == 6,
          "instance kinds: six values kept as properties (D1.1)");
    check(count(dynamic, "lengthConverted") == 2, "instance kinds: two Lengths converted");
    check(count(dynamic, "attributeNotImported") == 0, "instance kinds: no value is unsupported");
    check(levelBalances(dynamic, "instance") && levelBalances(dynamic, "definition"),
          "instance kinds: each level balances (D1.2)");
    const auto instance = dynamic["accounting"].toObject()["instance"].toObject();
    check(instance["counted"].toInt() == 6 && instance["imported"].toInt() == 6 &&
              instance["omitted"].toInt() == 0,
          "instance kinds: the instance level is 6 counted, 6 imported, 0 omitted");
    const auto attributes = imported.report["accounting"].toObject()["attributes"].toObject();
    check(attributes["counted"].toInt() == 6 && attributes["imported"].toInt() == 6 &&
              attributes["omitted"].toInt() == 0,
          "instance kinds: the attribute accounting is 6 read = 6 imported (D1.2)");
    // The package keeps the Length in metres, as the helper converts it (D1.7 at the package).
    const auto package = packageOf(fixture("dc_instance_kinds.skp"), temp);
    bool lengthKept = false;
    for (const auto &attribute : package.definitions[0].instances[0].attributes)
        if (attribute.key == "LenX") {
            lengthKept = attribute.kind == SkpPackageAttribute::Kind::Length &&
                         metres(attribute.number, 12.0);
        }
    check(lengthKept, "instance kinds: the package keeps LenX as a Length of 0.3048 m");
}

// (b) definition-level values, formulas and onClick, the instance value that wins, and a nested
// placement.
void definitionKeys(const QTemporaryDir &temp) {
    auto imported = importOf(fixture("dc_definition_keys.skp"));
    const auto &doc = imported.document;
    const Body *deskA = placementNamed(doc, "Desk A");
    const Body *deskB = placementNamed(doc, "Desk B");
    const Body *deskC = nestedNamed(doc, "Desk C");
    check(deskA && deskB && deskC,
          "definition keys: Desk A and Desk B are placed, Desk C is nested in Room");
    // A definition's value reaches every placement of it (D1.1 at definition level).
    for (const Body *desk : {deskA, deskC}) {
        check(numberIs(desk, "skp.dynamic_attributes.LenX", 36.0 * 0.0254),
              "definition keys: LenX is 36 inches, 0.9144 m");
        check(textIs(desk, "skp.dynamic_attributes._LenX_formula", "LenX+10"),
              "definition keys: the formula is kept as text");
        check(textIs(desk, "skp.dynamic_attributes._material_formula", "IF(LenX>1,1,0)"),
              "definition keys: the material formula is kept as text");
        check(textIs(desk, "skp.dynamic_attributes.onClick", "ALERT(\"Hi\"); REDRAW()"),
              "definition keys: onClick is kept as text (D1.4)");
    }
    // The placement's own LenX wins over the definition's (D1.1: one key, one value on the
    // placement).
    check(numberIs(deskB, "skp.dynamic_attributes.LenX", 24.0 * 0.0254),
          "definition keys: Desk B keeps its own 24-inch LenX");
    check(textIs(deskB, "skp.dynamic_attributes._LenX_formula", "LenX+10"),
          "definition keys: Desk B still holds the formula");
    const auto dynamic = dynamicOf(imported);
    check(count(dynamic, "metadataImported") == 5,
          "definition keys: four definition values and one instance value kept");
    check(count(dynamic, "lengthConverted") == 2,
          "definition keys: the definition's LenX and Desk B's LenX converted");
    check(count(dynamic, "formulaNotEvaluated") == 1,
          "definition keys: one formula kept, not evaluated");
    check(count(dynamic, "materialFormulaNotApplied") == 1 &&
              imported.report["notices"].toObject()["materialFormulaNotApplied"].toInt() == 1,
          "definition keys: the material formula is reported as not applied");
    check(count(dynamic, "behaviourNotRun") == 1 &&
              imported.report["notices"].toObject()["behaviourNotRun"].toInt() == 1,
          "definition keys: onClick is reported as not run (D1.4)");
    check(count(dynamic, "copiesNotReproduced") == 0, "definition keys: no Copies");
    check(imported.report["notices"].toObject()["propertiesDropped"].toInt() == 1,
          "definition keys: the definition's LenX is dropped on Desk B, where the placement's own "
          "names it");
    const auto definition = dynamic["accounting"].toObject()["definition"].toObject();
    check(definition["counted"].toInt() == 4 && definition["imported"].toInt() == 4 &&
              definition["omitted"].toInt() == 0,
          "definition keys: four definition values read, four imported (D1.2)");
    const auto instance = dynamic["accounting"].toObject()["instance"].toObject();
    check(instance["counted"].toInt() == 1 && instance["imported"].toInt() == 1,
          "definition keys: one instance value, kept");
    check(levelBalances(dynamic, "instance") && levelBalances(dynamic, "definition"),
          "definition keys: each level balances (D1.2)");
    const auto attributes = imported.report["accounting"].toObject()["attributes"].toObject();
    check(attributes["counted"].toInt() == 5 && attributes["imported"].toInt() == 5 &&
              attributes["omitted"].toInt() == 0,
          "definition keys: the attribute accounting is 5 read = 5 imported");
    // Formulas and onClick run nothing: the document holds one face per placed component, four in
    // all.
    check(facesIn(doc) == 4,
          "definition keys: the geometry is four faces, one per placed component");
    check(noParameterRecords(doc),
          "definition keys: no component parameter record is created (D1.3)");
    // The package keeps a definition's values with their kinds.
    const auto package = packageOf(fixture("dc_definition_keys.skp"), temp);
    bool lengthOnDefinition = false;
    for (const auto &definitionEntry : package.definitions)
        for (const auto &attribute : definitionEntry.attributes)
            if (attribute.key == "LenX" && attribute.kind == SkpPackageAttribute::Kind::Length)
                lengthOnDefinition = metres(attribute.number, 36.0);
    check(lengthOnDefinition,
          "definition keys: the package keeps the definition's LenX as a Length of 0.9144 m");
}

// (c) Copies: a placement with Copies above 0 imports as one placement (D1.5), and a formula under
// the cap.
void copiesLimits(const QTemporaryDir &temp) {
    (void)temp;
    auto imported = importOf(fixture("dc_copies_limits.skp"));
    const auto &doc = imported.document;
    check(placementNamed(doc, "Shelf A") != nullptr && placementNamed(doc, "Shelf B") != nullptr,
          "copies: both placements are imported");
    check(facesIn(doc) == 2,
          "copies: Shelf A with Copies 3 is one placement, so two faces in all (D1.5)");
    const Body *shelfA = placementNamed(doc, "Shelf A");
    check(numberIs(shelfA, "skp.dynamic_attributes.Copies", 3.0),
          "copies: Copies 3 is kept as a property");
    check(textLength(shelfA, "skp.dynamic_attributes._Qty_formula", 200),
          "copies: a 200-byte formula is kept whole");
    const auto dynamic = dynamicOf(imported);
    check(count(dynamic, "copiesNotReproduced") == 1,
          "copies: one placement holds Copies above 0 (D1.5)");
    check(count(dynamic, "formulaNotEvaluated") == 1, "copies: one formula kept, not evaluated");
    check(count(dynamic, "metadataImported") == 3, "copies: three values kept");
    check(imported.report["counts"].toObject()["placements"].toInt() == 2,
          "copies: two placements are counted");
    check(levelBalances(dynamic, "instance"), "copies: the instance level balances (D1.2)");
}

// The in-test package edits: an over-cap string, once per value, at both levels (D1.3 clipping
// counted).
void clipping(const QTemporaryDir &temp) {
    auto package = packageOf(fixture("dc_copies_limits.skp"), temp);
    const auto shelf =
        std::find_if(package.definitions.begin(), package.definitions.end(),
                     [](const SkpPackageDefinition &d) { return d.name == "Shelf"; });
    check(shelf != package.definitions.end() && !package.definitions[0].instances.empty(),
          "clipping: the fixture has the Shelf definition and its placements");
    if (shelf == package.definitions.end())
        return;
    SkpPackageAttribute deep;
    deep.dictionary = "dynamic_attributes";
    deep.key = "_Deep_formula";
    deep.kind = SkpPackageAttribute::Kind::String;
    deep.text = std::string(3000, 'd');
    shelf->attributes.push_back(deep); // a definition-level string over the 2,048-byte cap
    SkpPackageAttribute longText = deep;
    longText.key = "_Long_formula";
    longText.text = std::string(3000, 'l');
    for (auto &instance : package.definitions[0].instances)
        if (instance.definition == std::distance(package.definitions.begin(), shelf)) {
            instance.attributes.push_back(
                longText); // an instance-level string over the cap, on Shelf A only
            break;
        }
    const auto imported = importSkpPackage(package);
    const auto notices = imported.report["notices"].toObject();
    const auto dynamic = dynamicOf(imported);
    check(notices["truncatedAttributes"].toInt() == 2,
          "clipping: the instance string and the definition string are cut, the definition's once "
          "(not per placement)");
    check(count(dynamic, "textTruncated") == 2,
          "clipping: two Dynamic Component texts cut, the definition's once");
    check(count(dynamic, "formulaNotEvaluated") == 3,
          "clipping: three formulas kept as text, the definition's once");
    const Body *shelfA = placementNamed(imported.document, "Shelf A");
    const Body *shelfB = placementNamed(imported.document, "Shelf B");
    check(textLength(shelfA, "skp.dynamic_attributes._Long_formula", 2048),
          "clipping: the instance string is kept at 2,048 bytes");
    check(textLength(shelfA, "skp.dynamic_attributes._Deep_formula", 2048) &&
              textLength(shelfB, "skp.dynamic_attributes._Deep_formula", 2048),
          "clipping: the definition's string is kept at 2,048 bytes on both placements");
}

// A value the package carries without a value (a vector, an array) is counted and not imported
// (D1.2).
void unsupportedValues(const QTemporaryDir &temp) {
    auto package = packageOf(fixture("dc_copies_limits.skp"), temp);
    const auto shelf =
        std::find_if(package.definitions.begin(), package.definitions.end(),
                     [](const SkpPackageDefinition &d) { return d.name == "Shelf"; });
    if (shelf == package.definitions.end())
        return;
    SkpPackageAttribute vector;
    vector.dictionary = "dynamic_attributes";
    vector.key = "Pos";
    vector.kind = SkpPackageAttribute::Kind::Unsupported;
    vector.text = "vector";
    shelf->attributes.push_back(vector);
    for (auto &instance : package.definitions[0].instances)
        if (instance.definition == std::distance(package.definitions.begin(), shelf) &&
            instance.name == "Shelf A") {
            SkpPackageAttribute array = vector;
            array.key = "Path";
            array.text = "array";
            instance.attributes.push_back(array);
        }
    const auto imported = importSkpPackage(package);
    const Body *shelfA = placementNamed(imported.document, "Shelf A");
    check(!propertyOf(shelfA, "skp.dynamic_attributes.Path") &&
              !propertyOf(shelfA, "skp.dynamic_attributes.Pos"),
          "unsupported: neither the vector nor the array is a property");
    const auto dynamic = dynamicOf(imported);
    check(count(dynamic, "attributeNotImported") == 2,
          "unsupported: two values the package cannot carry, counted");
    check(imported.report["notices"].toObject()["attributeNotImported"].toInt() == 2,
          "unsupported: the notice names the two");
    // Shelf A holds Copies, _Qty_formula and the array; Shelf B holds Copies. The vector is on the
    // definition.
    const auto instance = dynamic["accounting"].toObject()["instance"].toObject();
    check(instance["counted"].toInt() == 4 && instance["imported"].toInt() == 3 &&
              instance["omitted"].toInt() == 1,
          "unsupported: the instance level is 4 read, 3 imported, the array omitted");
    const auto definition = dynamic["accounting"].toObject()["definition"].toObject();
    check(definition["counted"].toInt() == 1 && definition["imported"].toInt() == 0 &&
              definition["omitted"].toInt() == 1,
          "unsupported: the definition's vector is read and omitted");
    check(levelBalances(dynamic, "instance") && levelBalances(dynamic, "definition"),
          "unsupported: each level balances");
}

// Formulas are inert (D1.3): removing them changes no geometry, and changing an input changes only
// the metadata. Neither creates a parameter record.
void formulasAreInert(const QTemporaryDir &temp) {
    const auto original = packageOf(fixture("dc_definition_keys.skp"), temp);
    const auto baseline = importSkpPackage(original);
    const auto bodies = baseline.document.bodies().size();
    const auto faces = facesIn(baseline.document);

    auto withoutFormulas = original;
    for (auto &definition : withoutFormulas.definitions) {
        auto &attributes = definition.attributes;
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                                        [](const SkpPackageAttribute &a) {
                                            return a.key == "_LenX_formula" ||
                                                   a.key == "_material_formula" ||
                                                   a.key == "onClick";
                                        }),
                         attributes.end());
    }
    const auto noFormulas = importSkpPackage(withoutFormulas);
    check(noFormulas.document.bodies().size() == bodies && facesIn(noFormulas.document) == faces,
          "inert: removing the formulas and onClick changes no geometry");
    check(noParameterRecords(noFormulas.document),
          "inert: no component parameter record without the formulas");

    auto changed = original;
    for (auto &definition : changed.definitions)
        for (auto &attribute : definition.attributes)
            if (attribute.key == "LenX" && attribute.kind == SkpPackageAttribute::Kind::Length)
                attribute.number = 40.0 * 0.0254; // the definition's input, 36 to 40 inches
    for (auto &definition : changed.definitions)
        for (auto &instance : definition.instances)
            for (auto &attribute : instance.attributes)
                if (attribute.key == "LenX")
                    attribute.number = 30.0 * 0.0254; // Desk B's input, 24 to 30 inches
    const auto moved = importSkpPackage(changed);
    check(moved.document.bodies().size() == bodies && facesIn(moved.document) == faces,
          "inert: changing an input changes no geometry");
    check(noParameterRecords(moved.document),
          "inert: changing an input creates no parameter record");
    check(numberIs(placementNamed(moved.document, "Desk A"), "skp.dynamic_attributes.LenX",
                   40.0 * 0.0254),
          "inert: the changed input is the metadata value");
}

// D1.9, the source scan: no socket, network or HTTP code in the helper's or the import's sources.
void noNetworkSource() {
    const std::vector<std::string> forbidden = {
        "sys/socket.h", "netinet/",   "arpa/inet.h", "netdb.h",    "getaddrinfo", "gethostby",
        "QNetwork",     "QTcpSocket", "QUdpSocket",  "QSslSocket", "QWebSocket",  "QTcpServer",
        "http://",      "https://",   "curl",        "socket("};
    // Every file of src/skp, and the application's skp_* files in src/io.
    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::directory_iterator(SOURCE_DIR "/src/skp"))
        files.push_back(entry.path());
    for (const auto &entry : std::filesystem::directory_iterator(SOURCE_DIR "/src/io"))
        if (entry.path().filename().string().rfind("skp_", 0) == 0)
            files.push_back(entry.path());
    check(files.size() >= 10, "no network: the scan reads the helper's and the import's sources");
    for (const auto &file : files) {
        std::ifstream in(file);
        std::stringstream text;
        text << in.rdbuf();
        for (const auto &token : forbidden)
            check(text.str().find(token) == std::string::npos,
                  file.filename().string() + " holds \"" + token +
                      "\": no network code belongs in the import (D1.9)");
    }
}

// D1.9, the namespace run: the helper under `unshare -rn` (no network) imports the fixture. Skipped
// (77) when the runner does not allow a network namespace, as the CI container does not (ADR 0183).
int noNetworkRun(const QTemporaryDir &temp) {
    QProcess probe;
    probe.start(QStringLiteral("unshare"), {"-rn", "true"});
    if (!probe.waitForFinished(20000) || probe.exitCode() != 0) {
        std::cout << "skipped: unshare -rn is not allowed here; the source scan stands alone\n";
        return 77;
    }
    const auto out = temp.path() + "/offline.skskpp";
    QProcess process;
    process.start(QStringLiteral("unshare"), {"-rn", QStringLiteral(SKP_WORKER_PATH), "--import",
                                              fixture("dc_definition_keys.skp"), "--output", out});
    const bool finished = process.waitForFinished(120000);
    check(finished && process.exitCode() == 0,
          "no network: the helper imports with no network (D1.9)");
    check(QFileInfo::exists(out), "no network: the offline import wrote its package");
    return 0;
}

// The owner's file, when it is on this machine (--real, SKETCHYUP_SKP_CORPUS). Only the aggregate
// counts are printed (ADR 0178 D1.8): no name, no value.
int realCorpus() {
    const QString corpus = qEnvironmentVariable("SKETCHYUP_SKP_CORPUS");
    const QString file =
        corpus.isEmpty() ? QString() : corpus + QStringLiteral("/BanyanHarbor.skp");
    if (file.isEmpty() || !QFile::exists(file)) {
        std::cout << "SKETCHYUP_SKP_CORPUS does not hold the owner's file; skipped\n";
        return 77;
    }
    const auto imported = importOf(file);
    const auto dynamic = dynamicOf(imported);
    std::cout << "owner corpus: instance values "
              << dynamic["accounting"].toObject()["instance"].toObject()["counted"].toInt()
              << ", definition values "
              << dynamic["accounting"].toObject()["definition"].toObject()["counted"].toInt()
              << ", Dynamic Component metadata kept " << count(dynamic, "metadataImported") << "\n";
    check(levelBalances(dynamic, "instance") && levelBalances(dynamic, "definition"),
          "owner corpus: each level balances (D1.2)");
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = argc > 1 && std::string(argv[1]) == "--real";
    if (real) {
        const int code = realCorpus();
        if (code == 77)
            return 77;
        return failures ? 1 : 0;
    }
    QTemporaryDir temp;
    check(temp.isValid(), "the temporary directory");
    instanceKinds(temp);
    definitionKeys(temp);
    copiesLimits(temp);
    clipping(temp);
    unsupportedValues(temp);
    formulasAreInert(temp);
    noNetworkSource();
    const int offline = noNetworkRun(temp);
    if (failures)
        std::cerr << failures << " check(s) failed\n";
    else
        std::cout << "skp_dc: all checks passed"
                  << (offline == 77 ? " (the namespace run skipped)" : "") << "\n";
    return failures ? 1 : 0;
}
