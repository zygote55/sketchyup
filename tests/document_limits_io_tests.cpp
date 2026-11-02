// R082.gg native bounds at the document limits: a document at the materialized
// body and placement limits saves and reloads within the 4 MiB manifest bound,
// and over-limit files are rejected on load with bounded D06 diagnostics.
// R110 (1.7.0): 300,000 unique vertices load; one more face of 100 vertices does not.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/model.hpp"
#include "geometry/topology.hpp"
#include "io/document_io.hpp"
#include "io/native_limits.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
// A flat face of `sides` vertices at level `level` (z = 2 level): unique-vertex ballast. Faces
// of 100 vertices are the cheapest to build per vertex.
Id ring(Document &doc, int level, int sides = 100) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({std::cos(angle), std::sin(angle), 2.0 * level});
    }
    return doc.addFace({loop});
}
// A closed loop (or open path) of `count` distinct vertices, one wire per side, with `chords` extra
// wires from the first vertex (see document_limits_tests).
BodyPtr polyline(Id id, size_t count, bool closed, size_t chords = 0) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Polyline " + std::to_string(id);
    auto &surface = body->surface;
    std::vector<Id> ringIds;
    for (size_t i = 0; i < count; ++i) {
        const double angle = 2 * std::numbers::pi * double(i) / double(count);
        const Id vertex = surface.nextId++;
        surface.vertices.emplace(vertex, Vec3{1000 * std::cos(angle), 1000 * std::sin(angle),
                                              10.0 * double(id)});
        ringIds.push_back(vertex);
    }
    const size_t sides = closed ? count : count - 1;
    for (size_t i = 0; i < sides; ++i)
        surface.wires.push_back({ringIds[i], ringIds[(i + 1) % count]});
    for (size_t k = 0; k < chords; ++k)
        surface.wires.push_back({ringIds[0], ringIds[k + 2]});
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}

// A copy of the stored document in which the first definition gains one member: a copy of its
// member with `vertexCount` vertices, under a new member identity.
QJsonObject withDuplicateMember(QJsonObject json, qsizetype vertexCount) {
    auto definitions = json["definitions"].toArray();
    auto definition = definitions.first().toObject();
    auto members = definition["members"].toArray();
    QJsonObject copy;
    for (const auto value : members)
        if (value.toObject()["vertices"].toArray().size() == vertexCount) {
            copy = value.toObject();
            break;
        }
    check(!copy.isEmpty(), "The stored definition has a member with the duplicated vertex count");
    const auto added = definition["nextMemberId"].toString();
    copy["id"] = added;
    members.append(copy);
    definition["members"] = members;
    definition["nextMemberId"] = QString::number(added.toULongLong() + 1);
    definitions[0] = definition;
    json["definitions"] = definitions;
    return json;
}

Id emptyGroups(Document &doc, size_t count) {
    const auto first = doc.nextId();
    Edit fill{"Fill", {}};
    for (size_t i = 0; i < count; ++i) {
        auto group = std::make_shared<Body>();
        group->id = first + i;
        group->kind = BodyKind::Group;
        group->name = "Empty";
        fill.changes.push_back({group->id, nullptr, group});
    }
    doc.apply(std::move(fill), doc.revision());
    return first;
}
// 200 placements of a row nesting 100 triangle placements (199 records each),
// plus 200 loose groups: exactly 20,000 placements and 40,000 materialized bodies.
Document atLimits() {
    Document doc;
    const auto triangle =
        createComponent(doc, doc.addFace({{{0, 0, 0}, {.5, 0, 0}, {0, .5, 0}}}), "Triangle");
    std::set<Id> members{triangle.instance};
    for (int i = 1; i < 99; ++i)
        members.insert(placeComponent(doc, triangle.definition,
                                      Transform::translation({1.0 * i, 0, 0}))
                           .instance);
    const auto row = createComponent(doc, createGroup(doc, members, "Row"), "Row");
    for (int i = 1; i < 200; ++i)
        placeComponent(doc, row.definition, Transform::translation({0, 1.0 * i, 0}));
    emptyGroups(doc, DocumentLimits::materialized.bodies - doc.bodies().size());
    return doc;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        static_assert(NativeLimits::manifestBytes == 4 * 1024 * 1024);
        static_assert(NativeLimits::containerBytes <= NativeLimits::fileBytes);
        auto doc = atLimits();
        check(doc.instances().size() == DocumentLimits::materialized.placements &&
                  doc.bodies().size() == DocumentLimits::materialized.bodies,
              "Fixture meets the placement and materialized body limits exactly");
        // The manifest carries allocator floors for every materialized body.
        const auto container = encodeContainer(doc);
        const auto manifest = qFromLittleEndian<quint32>(container.constData() + 12);
        check(manifest <= NativeLimits::manifestBytes, "Manifest fits its bound");
        const auto reloaded = decodeContainer(container);
        check(reloaded.bodies().size() == doc.bodies().size() &&
                  reloaded.instances().size() == doc.instances().size() &&
                  reloaded.materializedTotals() == doc.materializedTotals(),
              "A document at the limits reloads unchanged");
        check(encodeContainer(reloaded) == container, "Reloaded document saves identically");
        // R110 (ADR 0005, decision 5): this model is within the 1.6.1 bounds, so 1.6.1 reads it.
        check(encodeDocument(doc).size() <= qsizetype(32 * 1024 * 1024) &&
                  container.size() <= qsizetype(128 * 1024 * 1024),
              "The at-limit model is within the 1.6.1 bounds (32 MiB document, 128 MiB file)");
        std::cout << "manifest " << manifest << " bytes, container " << container.size()
                  << " bytes at " << doc.bodies().size() << " bodies\n";

        // One more placement record in the file is rejected before any expansion.
        auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
        auto instances = json["instances"].toArray();
        instances.append(instances.first());
        json["instances"] = instances;
        check(rejection([&] { decodeDocument(QJsonDocument(json).toJson()); }) ==
                  "Document load exceeds document placement limits: placements would be 20,001 "
                  "of 20,000 (stored component instances)",
              "An over-limit placement count is rejected on load");

        // Unique geometry: 10,000 loose groups load; one more record does not.
        Document loose;
        emptyGroups(loose, DocumentLimits::unique.bodies);
        auto looseJson = QJsonDocument::fromJson(encodeDocument(loose)).object();
        check(decodeDocument(QJsonDocument(looseJson).toJson()).bodies().size() ==
                  DocumentLimits::unique.bodies,
              "A document at the unique body limit loads");
        auto bodies = looseJson["bodies"].toArray();
        auto extra = bodies.last().toObject();
        const auto id = looseJson["nextId"].toString();
        extra["id"] = id;
        bodies.append(extra);
        looseJson["bodies"] = bodies;
        looseJson["nextId"] = QString::number(id.toULongLong() + 1);
        check(rejection([&] { decodeDocument(QJsonDocument(looseJson).toJson()); }) ==
                  "Document load exceeds document unique geometry limits: bodies would be "
                  "10,001 of 10,000",
              "An oversized unique-geometry file is rejected on load");
        // R110: 300,000 unique vertices (3,000 faces of 100) load. A file with one more face of
        // 100 vertices is refused on load, naming the new maximum.
        Document vertices;
        for (int i = 0; i < 3000; ++i)
            ring(vertices, i);
        const auto atVertexLimit = encodeDocument(vertices);
        check(decodeDocument(atVertexLimit).materializedTotals().vertices ==
                  DocumentLimits::unique.vertices,
              "A document at the unique vertex limit loads");
        auto vertexJson = QJsonDocument::fromJson(atVertexLimit).object();
        auto vertexBodies = vertexJson["bodies"].toArray();
        auto extraFace = vertexBodies.last().toObject();
        const auto vertexId = vertexJson["nextId"].toString();
        extraFace["id"] = vertexId;
        vertexBodies.append(extraFace);
        vertexJson["bodies"] = vertexBodies;
        vertexJson["nextId"] = QString::number(vertexId.toULongLong() + 1);
        check(rejection([&] { decodeDocument(QJsonDocument(vertexJson).toJson()); }) ==
                  "Document load exceeds document unique geometry limits: vertices would be "
                  "300,100 of 300,000",
              "An oversized unique-vertex file is rejected on load");
        // R110: a document chunk declared over the 96 MiB model bound is refused by its range before
        // its bytes are read, and a raw JSON file over its 128 MiB bound is refused before it is parsed.
        {
            const auto manifestLength = qFromLittleEndian<quint32>(container.constData() + 12);
            auto manifest = QJsonDocument::fromJson(container.mid(16, manifestLength)).object();
            auto chunks = manifest["chunks"].toArray();
            auto document = chunks.first().toObject();
            document["bytes"] = QString::number(NativeLimits::modelBytes + 1);
            chunks[0] = document;
            manifest["chunks"] = chunks;
            const auto patchedManifest = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
            QByteArray patched = container.left(16); // magic, version, and the manifest length below
            qToLittleEndian<quint32>(quint32(patchedManifest.size()), patched.data() + 12);
            patched += patchedManifest + container.mid(16 + manifestLength);
            check(rejection([&] { decodeContainer(patched); }) ==
                      "Invalid document chunk range or encoding",
                  "A document chunk over the 96 MiB model bound is refused by its range");
            check(rejection([&] {
                      decodeContainer(QByteArray(qsizetype(NativeLimits::rawJsonBytes) + 1, ' '));
                  }) == "Raw document exceeds 128 MiB",
                  "A raw JSON file over the 128 MiB raw bound is refused by name");
        }
        // R110: a stored component definition over its vertex or wire limit is refused on load,
        // naming the definition. Each definition is at its limit (200,000), and a copy of its
        // two-vertex wire member makes one more vertex count or wire count.
        {
            std::map<Id, BodyPtr> bodies{{1, polyline(1, 100000, true)},
                                         {2, polyline(2, 99998, false)},
                                         {3, polyline(3, 2, false)}};
            Document edge;
            edge.restore("00000000000000000000000000000842", 4, std::move(bodies));
            (void)createComponent(edge, createGroup(edge, {1, 2, 3}, "Edge"), "Edge");
            const auto stored = encodeDocument(edge);
            check(decodeDocument(stored).materializedTotals().vertices == 200000,
                  "A definition of exactly 200,000 vertices loads");
            check(rejection([&] {
                      decodeDocument(QJsonDocument(withDuplicateMember(
                                                       QJsonDocument::fromJson(stored).object(), 2))
                                         .toJson());
                  }) == "Component definition exceeds component definition limits: vertices would "
                        "be 200,002 of 200,000 (definition 'Edge' #1)",
                  "A definition over 200,000 vertices is refused on load");
        }
        {
            std::map<Id, BodyPtr> bodies{{1, polyline(1, 100000, true)},
                                         {2, polyline(2, 99996, false, 4)},
                                         {3, polyline(3, 2, false)}};
            Document chord;
            chord.restore("00000000000000000000000000000843", 4, std::move(bodies));
            (void)createComponent(chord, createGroup(chord, {1, 2, 3}, "Chord"), "Chord");
            const auto stored = encodeDocument(chord);
            check(decodeDocument(stored).materializedTotals().wires == 200000,
                  "A definition of exactly 200,000 wires loads");
            check(rejection([&] {
                      decodeDocument(QJsonDocument(withDuplicateMember(
                                                       QJsonDocument::fromJson(stored).object(), 2))
                                         .toJson());
                  }) == "Component definition exceeds component definition limits: wires would be "
                        "200,001 of 200,000 (definition 'Chord' #1)",
                  "A definition over 200,000 wires is refused on load");
        }
        std::cout << "Document limits I/O: 40,000-body save/reload, placement, unique body and "
                     "unique vertex load checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
