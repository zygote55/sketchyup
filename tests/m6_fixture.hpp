#pragma once
#include "automation/commands.hpp"
#include "core/appearance.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "geometry/boolean.hpp"
#include "io/document_io.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <numbers>
namespace sketchy::m6 {
inline void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline QString id(Id value) { return QString::number(value); }
inline QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", id(doc.revision())},
            {"commands", commands}};
}
inline Id stock(Document &doc, Vec3 origin, Vec3 size) {
    const auto body =
        doc.addFace({{origin, origin + Vec3{size.x, 0, 0}, origin + Vec3{size.x, size.y, 0},
                      origin + Vec3{0, size.y, 0}}});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, size.z);
    return body;
}
inline double volume(const Document &doc, Id body) {
    const auto measured = measureEntity(doc, {body, SelectionKind::Body, 0});
    check(measured.world.volume.has_value(), "Integrated member is a validated closed solid");
    return *measured.world.volume;
}
inline Id kind(const Document &doc, const std::string &expected) {
    Id found{};
    for (const auto &[bodyId, body] : doc.bodies()) {
        const auto it = body->properties.find("recipe.kind");
        if (it != body->properties.end() && std::get_if<std::string>(&it->second) &&
            std::get<std::string>(it->second) == expected) {
            check(!found, "Integrated fixture has an unambiguous recipe root");
            found = bodyId;
        }
    }
    check(found != 0, "Integrated fixture contains the required assembly");
    return found;
}
inline void assemblies(const Document &doc) {
    check(std::abs(volume(doc, kind(doc, "gable-roof")) - 4.554) < 1e-5, "Integrated roof volume");
    check(std::abs(volume(doc, kind(doc, "straight-stairs")) - 4.368) < 1e-5,
          "Integrated stair volume");
    for (const auto &name : {std::string("table"), std::string("cabinet")}) {
        const auto root = kind(doc, name);
        const auto bounds = measureEntity(doc, {root, SelectionKind::Body, 0}).local.bounds;
        check(bounds && length(bounds->dimensions() -
                               (name == "table" ? Vec3{1.2, .8, .75} : Vec3{.9, .4, 1.2})) < 1e-6,
              "Integrated furniture envelope");
        double sum{};
        for (const auto &[bodyId, body] : doc.bodies())
            if (body->parent == root)
                sum += volume(doc, bodyId);
        check(std::abs(sum - (name == "table" ? .0455 : .059705856)) < 1e-7,
              "Integrated member material volume sum");
    }
    check(doc.hostedComponents().attachments.size() == 2,
          "Integrated room retains both hosted windows");
}
inline void notch(const Document &doc, Id body, double normalZ) {
    const auto &surface = doc.bodies().at(body)->surface;
    bool found{};
    for (const auto &[faceId, face] : surface.faces) {
        bool plane = true;
        for (const auto &loop : face.loops)
            for (const auto vertex : loop)
                plane &= std::abs(surface.vertices.at(vertex).z - .04) < 1e-6;
        if (plane && std::abs(surface.area(faceId) - .0036) < 1e-8 &&
            surface.normal(faceId).z * normalZ > .999999)
            found = true;
    }
    check(found,
          "Actual half-lap floor/ceiling has 60 mm square area, 40 mm height and correct normal");
}
struct Study {
    Document before, joined, placed;
    Id site{}, joint{}, left{}, right{};
    QJsonObject report;
};
inline Study study() {
    Study out;
    auto doc = loadDocument(QStringLiteral(SOURCE_DIR "/examples/m6-site-before.sketchyup"));
    for (const auto &[bodyId, body] : doc.bodies())
        if (body->name == "Site study" && !body->parent)
            out.site = bodyId;
    check(out.site != 0, "Existing integrated site root");
    assemblies(doc);
    const auto left = stock(doc, {0, 0, 0}, {.4, .06, .08});
    const auto right = stock(doc, {.17, -.17, 0}, {.06, .4, .08});
    const auto cutLeft = stock(doc, {.17, -.01, .04}, {.06, .08, .06});
    const auto cutRight = stock(doc, {.16, 0, -.02}, {.08, .06, .06});
    for (const auto body : {left, right, cutLeft, cutRight}) {
        const auto front =
            createMaterial(doc, "Joint front " + std::to_string(body), {.65f, .4f, .2f});
        const auto back =
            createMaterial(doc, "Joint back " + std::to_string(body), {.35f, .18f, .08f});
        assignMaterial(doc, body, {}, front, true, false);
        assignMaterial(doc, body, {}, back, false, true);
    }
    out.joint = createGroup(doc, {left, right, cutLeft, cutRight}, "Cross-lap joint");
    doc.transform(out.joint, Transform::translation({12, 0, 0}), out.site);
    out.before = doc.readSnapshot();
    auto trim = [&](Id body, Id tool) {
        return QJsonObject{{"command", "geometry.trim"},
                           {"body", id(body)},
                           {"tool", id(tool)},
                           {"context", id(out.joint)},
                           {"keepTarget", false}};
    };
    const auto request =
        batch(doc, {trim(left, cutLeft), trim(right, cutRight),
                    QJsonObject{{"command", "geometry.delete"}, {"body", id(cutLeft)}},
                    QJsonObject{{"command", "geometry.delete"}, {"body", id(cutRight)}}});
    const auto original = encodeContainer(doc);
    const auto preview = previewBatch(doc, request);
    check(encodeContainer(doc) == original, "Integrated joinery preview remains private");
    const auto history = doc.history().total;
    const auto result = executeBatch(doc, request);
    check(result["solidOperations"] == preview["solidOperations"] &&
              doc.history().total == history + 1,
          "Both complementary cuts and cleanup publish as one Undo task");
    const auto operations = result["solidOperations"].toArray();
    check(operations.size() == 2, "Both joinery cuts report independently");
    for (int index = 0; index < 2; ++index) {
        const auto parts = operations[index].toObject()["parts"].toArray();
        check(parts.size() == 1, "Each cut retains one continuous stock member");
        const auto part = parts[0].toObject();
        const auto body = part["body"].toString().toULongLong();
        (index ? out.right : out.left) = body;
        check(std::abs(volume(doc, body) - .001776) < 1e-8, "Analytical half-lap material volume");
        notch(doc, body, index ? -1 : 1);
        check(part["faces"].toArray().size() ==
                  qsizetype(doc.bodies().at(body)->surface.faces.size()),
              "Every joinery face has retained provenance");
        for (const auto &value : part["faces"].toArray()) {
            const auto face = value.toObject();
            const auto generated = face["face"].toString().toULongLong();
            const auto source = face["sourceBody"].toString().toULongLong();
            const auto sourceFace = face["sourceFace"].toString().toULongLong();
            const auto &before = *out.before.bodies().at(source), &after = *doc.bodies().at(body);
            const bool reversed =
                dot(before.surface.normal(sourceFace), after.surface.normal(generated)) < 0;
            auto sides = faceMaterials(before, sourceFace);
            if (reversed)
                std::swap(sides.front, sides.back);
            check(face["reversed"].toBool() == reversed && faceMaterials(after, generated) == sides,
                  "Every generated face preserves independently checked physical material sides");
        }
    }
    const auto overlap =
        booleanSolids(doc.bodies().at(out.left)->surface, doc.bodies().at(out.right)->surface,
                      BooleanOperation::Intersect);
    check(overlap.parts.empty() && overlap.volume == 0,
          "Complementary half-lap members have no overlapping material");
    for (const auto &[bodyId, body] : out.before.bodies())
        if (bodyId != left && bodyId != right && bodyId != cutLeft && bodyId != cutRight)
            check(*doc.bodies().at(bodyId) == *body,
                  "Joinery edits preserve every unrelated study record");
    const auto cutBodies = doc.bodies();
    doc.undo();
    check(doc.bodies() == out.before.bodies() && doc.materials() == out.before.materials(),
          "One integrated Undo restores exact source records and materials");
    doc.redo();
    check(doc.bodies() == cutBodies, "One Redo restores all mapped joinery records");
    const auto edge = doc.bodies().at(out.left)->topology.edges.begin()->first;
    executeBatch(doc,
                 batch(doc, {QJsonObject{{"command", "geometry.edge_appearance"},
                                         {"context", id(out.joint)},
                                         {"entities", QJsonArray{QJsonObject{{"body", id(out.left)},
                                                                             {"edge", id(edge)}}}},
                                         {"hidden", true}}}));
    check(doc.bodies().at(out.left)->surface == cutBodies.at(out.left)->surface &&
              doc.bodies().at(out.left)->faceMaterials == cutBodies.at(out.left)->faceMaterials,
          "A chained appearance edit preserves mapped faces and material assignments");
    out.joined = doc.readSnapshot();
    executeBatch(doc, batch(doc, {QJsonObject{{"command", "assembly.site_place"},
                                              {"body", id(out.site)},
                                              {"position", QJsonArray{100000125, 200000250, 12500}},
                                              {"positionUnit", "mm"},
                                              {"frame", "world"},
                                              {"yawDeltaRadians", std::numbers::pi / 6}}}));
    for (const auto &[bodyId, body] : out.joined.bodies())
        if (bodyId != out.site)
            check(*doc.bodies().at(bodyId) == *body,
                  "Placed study preserves all local IDs, topology and materials");
    check(doc.hostedComponents() == out.joined.hostedComponents() &&
              doc.definitions() == out.joined.definitions() &&
              doc.instances() == out.joined.instances(),
          "Placed integrated study retains hosted and shared components");
    assemblies(doc);
    for (const auto body : {out.left, out.right})
        check(std::abs(volume(doc, body) - .001776) < 1e-8,
              "Distant joint retains material volume");
    const auto saved = encodeContainer(doc);
    check(encodeContainer(decodeContainer(saved)) == saved,
          "Complete integrated study reopens byte-for-byte");
    out.placed = doc.readSnapshot();
    Document invalid;
    const auto open = invalid.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
    const auto tool = stock(invalid, {.1, .1, -1}, {.2, .2, 2});
    const auto unchanged = encodeContainer(invalid);
    bool rejected{};
    try {
        executeBatch(invalid, batch(invalid, {QJsonObject{{"command", "geometry.trim"},
                                                          {"body", id(open)},
                                                          {"tool", id(tool)},
                                                          {"context", "0"},
                                                          {"keepTarget", false}}}));
    } catch (const BooleanError &error) {
        rejected =
            error.code() == "BOOLEAN_INVALID_SOLID" && error.report().status == "open_boundary";
    }
    check(rejected && encodeContainer(invalid) == unchanged,
          "Open stock reports solid prerequisites without mutation");
    out.report = {{"halfLapMemberVolume", .001776},
                  {"jointVolume", .003552},
                  {"stockLength", .4},
                  {"stockWidth", .06},
                  {"stockHeight", .08},
                  {"notchDepth", .04},
                  {"overlapVolume", 0},
                  {"roofVolume", volume(doc, kind(doc, "gable-roof"))},
                  {"stairVolume", volume(doc, kind(doc, "straight-stairs"))},
                  {"site", id(out.site)},
                  {"joint", id(out.joint)},
                  {"left", id(out.left)},
                  {"right", id(out.right)},
                  {"solidOperations", operations},
                  {"privatePreview", true},
                  {"oneUndoRedo", true},
                  {"materialsAndMappingsPreserved", true},
                  {"exactReopen", true},
                  {"invalidSolidRejected", true}};
    return out;
}
inline void retain(Study &study, const QString &directory) {
    check(QDir().mkpath(directory), "Create integrated evidence directory");
    saveDocument(study.before, directory + "/m6-joinery-before.sketchyup");
    saveDocument(study.joined, directory + "/m6-joinery-after.sketchyup");
    saveDocument(study.placed, directory + "/m6-study-after.sketchyup");
    QFile file(directory + "/measurements.json");
    check(file.open(QIODevice::WriteOnly | QIODevice::NewOnly),
          "Fresh integrated measurement report");
    const auto data = QJsonDocument(study.report).toJson();
    check(file.write(data) == data.size(), "Write integrated measurements");
}
} // namespace sketchy::m6
