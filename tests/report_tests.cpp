// R101.a (ADR 0175 §11): the report engine's hand-computed fixtures Q1–Q4 and checks V1–V4, V6, V8
// and V9, the request refusals, determinism and the bounded report.run query.
//
// ADR 0175 does not name Q1–Q4; they are read here as four documents covering the fixture table:
//   Q1  components: BoxP ×3 on tag Boxes, Post, Top, Table ×2, Steel and Wood (V1, V2);
//   Q2  transforms: the scaled, mirrored and three-level copies (V4), and the hidden group and
//       hidden tag (V3, per element);
//   Q3  invalid solids: an open plate and shared-face cubes (V6), and 65 disjoint cubes in one body
//       on their own, over the 64-shell analysis limit (V6);
//   Q4  the whole quantities-a document with the terrain (V3 totals, V8, V9).
//
// Where an ADR hand total is wrong, the test asserts the corrected arithmetic, with the ADR value
// named in a comment; see docs/decisions/0175 "Implementation notes (R101.a)".
#include "automation/inspection.hpp"
#include "automation/report_commands.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/report.hpp"
#include "core/selection.hpp"
#include "core/tags.hpp"
#include "core/terrain.hpp"
#include "core/terrain_records.hpp"
#include "core/transform.hpp"
#include "presentation/unit_display.hpp"
#include "report_fixtures.hpp"
#include <QJsonDocument>
#include <QLocale>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

using namespace sketchy;
using namespace sketchy::report_fixtures;

namespace {

void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool near(double actual, double expected, double relative = 1e-9) {
    return std::abs(actual - expected) <= relative * std::max(1., std::abs(expected));
}

// The tolerance the ADR allows for the feet conversions (1e-4 relative).
bool nearFeet(double actual, double expected) { return near(actual, expected, 1e-4); }

ReportRequest request(bool counts = true, bool areas = true, bool volumes = true) {
    ReportRequest result;
    result.counts = counts;
    result.areas = areas;
    result.volumes = volumes;
    return result;
}

// The row of a group with the given key, or nullptr.
const ReportCountRow *countRow(const ReportResult &result, const std::string &key) {
    for (const auto &row : result.counts)
        if (row.key == key)
            return &row;
    return nullptr;
}

const ReportVolumeRow *volumeRow(const ReportResult &result, const std::string &key) {
    for (const auto &row : result.volumes)
        if (row.key == key)
            return &row;
    return nullptr;
}

double areaSum(const ReportResult &result, const std::string &key = {}) {
    double total = 0;
    for (const auto &row : result.areas)
        if (key.empty() || row.key == key)
            total += row.area;
    return total;
}

double areaOf(const ReportResult &result, const std::string &key, Id material) {
    for (const auto &row : result.areas)
        if (row.key == key && row.material == material)
            return row.area;
    return -1;
}

std::string idText(Id id) { return std::to_string(id); }

// The area and volume of the one body in `doc`, from the default report.
std::pair<double, double> verifyPlain(const Document &doc) {
    const auto result = runReport(doc, request());
    return {areaSum(result), volumeRow(result, "All")->volume};
}

// Q2: the transform copies, each in its own document so its own totals are exact.
void transforms() {
    Document reference;
    box(reference, {0, 0, 0}, {2, 2, 1});
    const auto plain = verifyPlain(reference);
    check(near(plain.first, 16) && near(plain.second, 4), "Q2 reference box: 16 m², 4 m³");

    Document mirrored;
    const auto mirror = box(mirrored, {0, 0, 0}, {2, 2, 1});
    mirrored.transform(mirror, Transform::scaling({-1, 1, 1}));
    const auto mirroredTotals = verifyPlain(mirrored);
    check(near(mirroredTotals.first, plain.first) && near(mirroredTotals.second, plain.second),
          "V4: the mirrored copy equals the unscaled box, in area and in volume");

    Document scaled;
    const auto scale = box(scaled, {0, 0, 0}, {2, 2, 1});
    scaled.transform(scale, Transform::scaling({2, 2, 2}));
    const auto scaledTotals = verifyPlain(scaled);
    check(near(scaledTotals.first, 4 * plain.first) && near(scaledTotals.second, 8 * plain.second),
          "V4: a 2x scale is x4 area and x8 volume");

    // Root group at 1.5, child group at 2, a 1 m cube: a linear factor of 3.
    Document chain;
    const auto cube = box(chain, {0, 0, 0}, {1, 1, 1});
    const auto child = createGroup(chain, {cube}, "Child");
    const auto root = createGroup(chain, {child}, "Root");
    // transform() sets the parent as well: the child keeps its root, the root stays top-level.
    chain.transform(child, Transform::scaling({2, 2, 2}), root);
    chain.transform(root, Transform::scaling({1.5, 1.5, 1.5}));
    const auto totals = runReport(chain, request());
    check(totals.counts.size() == 1 && totals.counts.front().contexts == 3,
          "The three-level chain has 2 groups and 1 box as contexts");
    // ADR 0175 §11 gives 9 m² here ("area x(3.0)² = 9 m²"). A 1 m cube has 6 m² of faces, and
    // the Stash row of the same ADR counts that cube as 6 m²; scaled by 3 linearly it is 54 m².
    check(near(volumeRow(totals, "All")->volume, 27) && near(areaSum(totals), 54),
          "V4: the three-level chain is 27 m³ and 54 m² (the ADR's 9 m² is an arithmetic error)");
}

// Q1: the component counting rules (V1, V2), with the material areas of the fixture.
void components() {
    Document doc;
    const auto q = buildComponents(doc);
    auto byDefinition = request();
    byDefinition.groupBy = ReportGroup::Definition;
    const auto all = runReport(doc, byDefinition);
    const auto *total = countRow(all, "All");
    check(
        total && total->instances == 15 && total->definitions == 4 && total->contexts == 28,
        "V1: 15 placements (3 BoxP, 2 Table, 10 nested), 4 definitions counted once, 28 contexts");
    check(near(areaOf(all, "All", q.steel), 36) && near(areaOf(all, "All", q.wood), 9.2) &&
              near(areaOf(all, "All", 0), 48),
          "Materials: Steel 36 m² (8 Post), Wood 9.2 m² (2 Top), no material 48 m² (BoxP)");

    auto table = request();
    table.definitions = {q.tableDefinition};
    const auto tables = runReport(doc, table);
    check(countRow(tables, "All")->instances == 12 && countRow(tables, "All")->contexts == 22,
          "V1: the Table filter returns 2 placements and their 10 nested placements (22 contexts)");

    auto boxes = request();
    boxes.groupBy = ReportGroup::Tag;
    const auto byTag = runReport(doc, boxes);
    const auto *row = countRow(byTag, idText(q.boxesTag));
    check(row && row->contexts == 6 && row->instances == 3 &&
              near(volumeRow(byTag, idText(q.boxesTag))->volume, 12) &&
              near(areaOf(byTag, idText(q.boxesTag), 0), 48),
          "V1: the Boxes row is 3 placements, 12 m³ and 48 m² (members carry the tag by "
          "inheritance)");

    auto post = request();
    post.groupBy = ReportGroup::Definition;
    post.definitions = {q.postDefinition};
    const auto posts = runReport(doc, post);
    check(countRow(posts, "All")->instances == 8 && countRow(posts, "All")->contexts == 16,
          "V2: the Post filter counts exactly the 8 nested placements, each once");
    check(countRow(posts, idText(q.tableDefinition)) &&
              countRow(posts, idText(q.tableDefinition))->instances == 8 &&
              countRow(posts, idText(q.postDefinition)) == nullptr,
          "V2: the nested Post placements and their members fall under the outer Table group");
}

// Q3: invalid solids are listed under their exact status and contribute no known volume (V6).
void invalidSolids() {
    Document doc;
    // An open plate: one face, no closed shell.
    doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 2, 0}, {0, 2, 0}}}, "Plate");
    // Two unit cubes sharing a face, as one body: the shared square is in both shells.
    auto pair = unitCube({0, 0, 0});
    append(pair, unitCube({0, 0, 1}));
    addSurface(doc, pair, "Shared-face cubes");

    const auto result = runReport(doc, request());
    check(result.invalid.size() == 2 && result.invalid[0].status == "open_boundary" &&
              result.invalid[1].status == "non_manifold" && result.invalid[0].contexts == 1 &&
              result.invalid[1].contexts == 1,
          "V6: each invalid context is listed under its exact status, in the CSV's order");
    check(volumeRow(result, "All")->volume == 0 && volumeRow(result, "All")->unvalidated == 2,
          "V6: no known volume, and two unvalidated contexts");
    check(near(areaSum(result), 14), "V6: the invalid solids keep their area (2 + 12 m²)");

    // Over the shell budget: 65 disjoint cubes in one body (analysis_limit).
    Document over;
    Surface many;
    for (int i = 0; i < 65; ++i)
        append(many, unitCube({2. * i, 0, 0}));
    addSurface(over, many, "Too many shells");
    const auto limited = runReport(over, request());
    check(limited.invalid.size() == 1 && limited.invalid[0].status == "analysis_limit" &&
              limited.invalid[0].contexts == 1,
          "V6: an over-budget shell is listed as analysis_limit");
    check(volumeRow(limited, "All")->volume == 0 && volumeRow(limited, "All")->unvalidated == 1 &&
              near(areaSum(limited), 390),
          "V6: analysis_limit is no known volume; its 390 m² of area still counts");
}

// Q4: the whole quantities-a document (V3 totals, V8, V9). The ADR's totals are corrected where
// its own arithmetic does not hold; each correction is named in a comment and in the notes.
void quantitiesA() {
    Document doc;
    const auto a = buildQuantitiesA(doc);
    const auto all = runReport(doc, request());
    const auto *contexts = countRow(all, "All");
    // 38 non-terrain: the two shared-face cubes are one body, and the degenerate body is absent.
    check(contexts && contexts->contexts == 38 + 1 + a.tiles,
          "Contexts: 38 non-terrain plus the terrain container and its tiles");
    check(near(volumeRow(all, "All")->volume, 82.4) && volumeRow(all, "All")->unvalidated == 2,
          "V3: known volume 82.4 m³ with two unvalidated contexts (BoxP 12, scaled 32, mirror 4, "
          "chain 27, Stash 1, Off 2, components 4.4)");
    // ADR 0175 §11 gives 40,174.2 m². Its own rows sum to 169 m² of non-terrain, non-component
    // area and 45.2 m² of components, which is 40,214.2 m² with the terrain. The chain row is
    // wrong: a 1 m cube scaled by 3 keeps 6 m² × 9 = 54 m², not 9 m². With that, the non-terrain
    // rows are 48 + 64 + 16 + 54 + 6 + 12 + 2 + 12 + 0 = 214 m² (the zero-thickness row at its ADR
    // value of 0 m², which this document cannot build), so the total is 214 + 45.2 + 40,000 =
    // 40,259.2 m². Totals with terrain use the ADR's terrain tolerance (1e-6 relative): the
    // terrain's vertices are quantized, so its 40,000 m² comes out as 40,000.00106 m².
    check(near(areaSum(all), 40259.2, 1e-6),
          "V3: area 40,259.2 m² (the ADR's 40,174.2 m² does not follow from its own rows)");
    check(all.terrain.size() == 1, "One terrain table row");
    const auto &terrain = all.terrain.front();
    check(terrain.terrain == a.terrain && terrain.triangles == 80000 &&
              terrain.contexts == 1 + a.tiles && near(terrain.area, 40000, 1e-6),
          "The terrain: 1 container and its tiles, 80,000 triangles, 40,000 m² flat");

    auto visible = request();
    visible.visibility = ReportVisibility::Visible;
    const auto shown = runReport(doc, visible);
    check(near(volumeRow(shown, "All")->volume, 79.4) && near(areaSum(shown), 40241.2, 1e-6),
          "V3: visible excludes Stash and Off: 79.4 m³ and 40,241.2 m²");
    check(all.invalid.size() == 2 && all.invalid[0].status == "open_boundary" &&
              all.invalid[1].status == "non_manifold",
          "V6 on the whole document: the two reachable invalid statuses, one context each");

    // V9: the stamp follows the revision; an edit moves it; undo and redo report each state
    // exactly.
    const auto identity = doc.identity();
    const auto first = doc.revision();
    doc.transform(a.stash, Transform::scaling({2, 2, 2}));
    const auto edited = runReport(doc, request());
    check(near(volumeRow(edited, "All")->volume, 89.4) && near(areaSum(edited), 40277.2, 1e-6),
          "V9: scaling Stash by 2 adds 7 m³ and 18 m² (its cube is 8 m³ and 24 m²)");
    check(doc.revision() > first && edited.documentId == identity &&
              edited.revision == doc.revision(),
          "V9: the edit increments the revision, and the stamp carries it");
    doc.undo();
    const auto undone = runReport(doc, request());
    // The ADR's V5 says undo gives a lower revision. ADR 0175 §4 and Document::undo increment it,
    // so a state is never stamped twice; the state itself is restored exactly.
    check(near(volumeRow(undone, "All")->volume, 82.4) && near(areaSum(undone), 40259.2, 1e-6) &&
              undone.revision > edited.revision,
          "V9: undo restores the totals; the revision moves forward, never back");
    doc.redo();
    const auto redone = runReport(doc, request());
    check(near(volumeRow(redone, "All")->volume, 89.4) && redone.revision > undone.revision,
          "V9: redo reports the edited state again under a new revision");
}

// V8: units and precision are stated, and their values are the converted ones.
void unitsAndPrecision() {
    Document doc;
    buildQuantitiesA(doc);
    auto describe = [&](QJsonObject json) {
        return reportDescription(doc, reportRequestFrom(doc, json, false), nullptr);
    };
    auto metadata = describe({}).value("metadata").toObject();
    check(metadata["units"].toObject()["length"] == "m" &&
              metadata["units"].toObject()["area"] == "m2" &&
              metadata["units"].toObject()["volume"] == "m3" && metadata["documentUnits"] == "m" &&
              metadata["precision"] == "full",
          "V8: metres are stated with base codes m, m2, m3 and precision full");
    check(describe({{"precision", 3}}).value("volumes").toArray()[0].toObject()["volume"] ==
              "82.400",
          "V8: an explicit precision of 3 gives three fixed decimals");

    doc.setDisplayUnits(DisplayUnit::Millimeters, 0);
    metadata = describe({}).value("metadata").toObject();
    check(metadata["units"].toObject()["volume"] == "mm3" && metadata["precision"] == 0 &&
              describe({}).value("volumes").toArray()[0].toObject()["volume"] == "82400000000",
          "V8: millimetres: 82.4 m³ is 82,400,000,000 mm³ at precision 0");

    doc.setDisplayUnits(DisplayUnit::FeetInches, 3);
    metadata = describe({}).value("metadata").toObject();
    check(metadata["units"].toObject()["length"] == "ft" &&
              metadata["units"].toObject()["area"] == "ft2" &&
              metadata["documentUnits"] == "ft-in" && metadata["precision"] == 3,
          "V8: feet and inches report base codes ft, ft2, ft3 and say ft-in");
    const auto feet =
        describe({}).value("volumes").toArray()[0].toObject()["volume"].toString().toDouble();
    check(nearFeet(feet, 82.4 * 35.3146667214), "V8: 82.4 m³ in cubic feet, within the ADR's 1e-4");

    // The ADR's example: the mirrored box, 4 m³ = 141.2587 ft³ and 16 m² = 172.2226 ft².
    Document mirror;
    const auto mirrored = box(mirror, {0, 0, 0}, {2, 2, 1});
    mirror.transform(mirrored, Transform::scaling({-1, 1, 1}));
    mirror.setDisplayUnits(DisplayUnit::FeetInches, fullDisplayPrecision);
    const auto imperial = reportDescription(mirror, reportRequestFrom(mirror, {}, false), nullptr);
    const auto cubicFeet =
        imperial.value("volumes").toArray()[0].toObject()["volume"].toString().toDouble();
    const auto squareFeet =
        imperial.value("areas").toArray()[0].toObject()["area"].toString().toDouble();
    check(nearFeet(cubicFeet, 141.2587) && nearFeet(squareFeet, 172.2226),
          "V8: the ADR's ft-in example, 4 m³ = 141.2587 ft³ and 16 m² = 172.2226 ft²");
    // The factors are those of the entity info panel's displayMeasure: the same value at the same
    // precision. The locale is fixed so that its decimal point is the one this check expects.
    QLocale::setDefault(QLocale::c());
    check(displayMeasure(4, 3, DisplayUnit::FeetInches, 3).startsWith("141.259 ft") &&
              displayMeasure(16, 2, DisplayUnit::FeetInches, 3).startsWith("172.223 ft") &&
              near(cubicFeet, 141.259, 1e-4) && near(squareFeet, 172.223, 1e-4),
          "V8: the feet factors agree with displayMeasure's");
}

// Request refusals: each refusal names its reason (ADR 0175 §1).
void refusals() {
    Document doc;
    buildComponents(doc);
    auto refuses = [&](QJsonObject json, const char *code, const char *reason,
                       bool editor = false) {
        try {
            reportRequestFrom(doc, json, editor);
        } catch (const InspectionError &error) {
            check(error.code() == code &&
                      std::string(error.what()).find(reason) != std::string::npos,
                  std::string("Refusal ") + error.code() + ": " + error.what() + " (expected " +
                      code + ", " + reason + ")");
            return;
        }
        throw std::runtime_error(std::string("Expected a refusal: ") + reason);
    };
    refuses({{"metrics", QJsonArray{}}}, "INVALID_REQUEST", "at least one metric");
    refuses({{"metrics", QJsonArray{"counts", "counts"}}}, "INVALID_REQUEST", "distinct");
    refuses({{"metrics", QJsonArray{"profit"}}}, "INVALID_REQUEST", "Unknown report metric");
    refuses({{"groupBy", "colour"}}, "INVALID_REQUEST", "Unknown report grouping");
    refuses({{"surprise", 1}}, "INVALID_REQUEST", "Unknown report request field");
    refuses({{"filters", QJsonObject{{"colour", "red"}}}}, "INVALID_REQUEST",
            "Unknown report filter");
    refuses({{"filters", QJsonObject{{"visibility", "hidden"}}}}, "INVALID_REQUEST", "visibility");
    refuses({{"filters", QJsonObject{{"tags", QJsonArray{"999"}}}}}, "NOT_FOUND",
            "Report tag does not exist");
    refuses({{"filters", QJsonObject{{"tags", QJsonArray{"01"}}}}}, "INVALID_REQUEST", "canonical");
    refuses({{"filters", QJsonObject{{"tags", QJsonArray{"1", "1"}}}}}, "INVALID_REQUEST",
            "distinct");
    refuses({{"filters", QJsonObject{{"materials", QJsonArray{"999"}}}}}, "NOT_FOUND",
            "Report material does not exist");
    refuses({{"filters", QJsonObject{{"definitions", QJsonArray{"999"}}}}}, "NOT_FOUND",
            "Component definition does not exist");
    refuses({{"filters", QJsonObject{{"selection", true}}}}, "INVALID_REQUEST",
            "unavailable in file-only queries");
    refuses({{"precision", 1.5}}, "INVALID_REQUEST", "full or an integer");
    refuses({{"precision", 7}}, "LIMIT_EXCEEDED", "0-6 for m");

    Document millimetres;
    buildComponents(millimetres);
    millimetres.setDisplayUnits(DisplayUnit::Millimeters);
    try {
        reportRequestFrom(millimetres, {{"precision", 4}}, false);
        throw std::runtime_error("Expected the mm precision refusal");
    } catch (const InspectionError &error) {
        check(std::string(error.code()) == "LIMIT_EXCEEDED" &&
                  std::string(error.what()).find("0-3 for mm") != std::string::npos,
              "A precision of 4 is refused for mm, naming 0-3");
    }

    // Core refusals: no metric, and a selection without its bodies.
    ReportRequest none;
    none.counts = none.areas = none.volumes = false;
    bool refused = false;
    try {
        runReport(doc, none);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "A report with no metric is refused");
    ReportRequest selected;
    selected.selection = true;
    refused = false;
    try {
        runReport(doc, selected);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "A selection report without editor bodies is refused");
}

// The same request on the same revision gives the same bytes; a new revision gives a new stamp.
void determinism() {
    Document doc;
    buildComponents(doc);
    auto describe = [&] {
        return QJsonDocument(
                   reportDescription(
                       doc, reportRequestFrom(doc, {{"groupBy", "definition"}}, false), nullptr))
            .toJson(QJsonDocument::Compact);
    };
    const auto once = describe();
    check(once == describe(), "The same request on the same revision gives identical bytes");
    const auto stamp = doc.revision();
    doc.transform(doc.instances().begin()->first, Transform::translation({1, 0, 0}));
    check(doc.revision() > stamp && describe() != once,
          "A new revision changes the stamp and the report reflects the edit");
}

// report.run through the bounded query path: envelope, stamp, refusals and selection.
void registry() {
    Document doc;
    const auto a = buildQuantitiesA(doc);
    QJsonObject query{{"apiVersion", 1},
                      {"query", "report.run"},
                      {"documentId", QString::fromStdString(doc.identity())},
                      {"expectedRevision", QString::number(doc.revision())},
                      {"request", QJsonObject{{"metrics", "all"}, {"groupBy", "kind"}}}};
    const auto result = inspectDocument(doc, query);
    const auto data = result["data"].toObject();
    check(result["query"] == "report.run" &&
              result["revision"] == QString::number(doc.revision()) &&
              result["documentId"] == QString::fromStdString(doc.identity()),
          "report.run returns the stamp in the envelope");
    check(data["metadata"].toObject()["revision"] == QString::number(doc.revision()) &&
              data["counts"].toArray().size() >= 4,
          "report.run carries the stamp in its metadata and a counts table per kind");

    auto stale = query;
    stale["expectedRevision"] = QString::number(doc.revision() + 1);
    bool refused = false;
    try {
        inspectDocument(doc, stale);
    } catch (const InspectionError &error) {
        refused = std::string(error.code()) == "STALE_REVISION";
    }
    check(refused, "report.run refuses a stale revision");

    auto badField = query;
    badField["request"] = QJsonObject{{"metrics", "all"}, {"surprise", 1}};
    refused = false;
    try {
        inspectDocument(doc, badField);
    } catch (const InspectionError &error) {
        refused = std::string(error.code()) == "INVALID_REQUEST";
    }
    check(refused, "The registry schema refuses an unknown request field");

    auto tooManyTags = query;
    QJsonArray tags;
    for (int i = 0; i < 129; ++i)
        tags.append("1");
    tooManyTags["request"] = QJsonObject{{"filters", QJsonObject{{"tags", tags}}}};
    refused = false;
    try {
        inspectDocument(doc, tooManyTags);
    } catch (const InspectionError &error) {
        refused = std::string(error.code()) == "LIMIT_EXCEEDED";
    }
    check(refused, "A tag list over 128 entries is refused as LIMIT_EXCEEDED");

    // The session's selection: the mirrored copy only (a hidden body cannot be selected).
    Selection selection;
    selection.apply(doc, {{a.mirrored, SelectionKind::Body, 0}}, SelectionMode::Replace);
    auto selected = query;
    selected["request"] = QJsonObject{{"metrics", QJsonArray{"counts", "volumes"}},
                                      {"filters", QJsonObject{{"selection", true}}}};
    const auto picked = inspectDocument(doc, selected, &selection).value("data").toObject();
    check(picked["counts"].toArray()[0].toObject()["contexts"].toInt() == 1 &&
              near(picked["volumes"].toArray()[0].toObject()["volume"].toString().toDouble(), 4),
          "A selection report counts the selected body alone (4 m³)");
    refused = false;
    try {
        inspectDocument(doc, selected);
    } catch (const InspectionError &error) {
        refused = std::string(error.code()) == "INVALID_REQUEST";
    }
    check(refused, "A selection report is refused outside an editor session");

    check(inspectionCapabilities()["queries"].toArray().size() > 0,
          "The inspection catalogue lists the queries");
}

} // namespace

int main() {
    try {
        transforms();
        components();
        invalidSolids();
        quantitiesA();
        unitsAndPrecision();
        refusals();
        determinism();
        registry();
        std::cout << "report tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "report tests failed: " << error.what() << '\n';
        return 1;
    }
}
