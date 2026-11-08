// R082.gg native bounds at the document limits (R115 values): a document with placements and
// loose bodies saves and reloads within the manifest bound, and over-limit files are rejected on
// load with bounded D06 diagnostics.
// R115 (2026-11-07): the manifest bound is 32 MiB, the model chunk 768 MiB and the raw JSON bound
// 1 GiB. The at-limit file of 80,000 placements and 320,000 bodies is built and saved on the GPU
// PC (docs/verification/R115-raised-limits.md). Here the limits are checked by constants and by
// counts, and the fixtures are small, so CI builds no more than it did before R115.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/model.hpp"
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

// A row nesting 100 triangle placements (199 records each), placed `rows` times more, with
// `groups` loose groups. Small: it exercises the same save, reload and placement records as the
// at-limit file, without its size.
Document fixture(int rows, size_t groups) {
    Document doc;
    const auto triangle =
        createComponent(doc, doc.addFace({{{0, 0, 0}, {.5, 0, 0}, {0, .5, 0}}}), "Triangle");
    std::set<Id> members{triangle.instance};
    for (int i = 1; i < 99; ++i)
        members.insert(placeComponent(doc, triangle.definition,
                                      Transform::translation({1.0 * i, 0, 0}))
                           .instance);
    const auto row = createComponent(doc, createGroup(doc, members, "Row"), "Row");
    for (int i = 1; i <= rows; ++i)
        placeComponent(doc, row.definition, Transform::translation({0, 1.0 * i, 0}));
    emptyGroups(doc, groups);
    return doc;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        // The R115 bounds: the manifest 32 MiB, the model chunk 768 MiB, the raw JSON 1 GiB, and the
        // file within the container bound.
        static_assert(NativeLimits::manifestBytes == 32 * 1024 * 1024);
        static_assert(NativeLimits::modelBytes == 768ull * 1024 * 1024);
        static_assert(NativeLimits::rawJsonBytes == 1024ull * 1024 * 1024);
        static_assert(NativeLimits::rawJsonBytes >= NativeLimits::modelBytes);
        static_assert(NativeLimits::containerBytes <= NativeLimits::fileBytes);

        auto doc = fixture(4, 200);
        check(doc.instances().size() == 500, "Fixture: 100 nested placements and four more rows");
        // The manifest carries allocator floors for every materialized body.
        const auto container = encodeContainer(doc);
        const auto manifest = qFromLittleEndian<quint32>(container.constData() + 12);
        check(manifest <= NativeLimits::manifestBytes, "Manifest fits its bound");
        const auto reloaded = decodeContainer(container);
        check(reloaded.bodies().size() == doc.bodies().size() &&
                  reloaded.instances().size() == doc.instances().size() &&
                  reloaded.materializedTotals() == doc.materializedTotals(),
              "A document with placements and bodies reloads unchanged");
        check(encodeContainer(reloaded) == container, "Reloaded document saves identically");
        std::cout << "manifest " << manifest << " bytes, container " << container.size()
                  << " bytes at " << doc.bodies().size() << " bodies\n";

        // The placement limit on load is checked by counts (80,001 placements, below).
        check(rejection([&] {
                  checkLimits({DocumentLimits::materialized.placements + 1}, LimitClass::Placement,
                              "Document load", "stored component instances");
              }) == "Document load exceeds document placement limits: placements would be "
                    "80,001 of 80,000 (stored component instances)",
              "An over-limit placement count is rejected on load, by name");
        // Unique geometry: a file of 200 loose groups loads; the unique body limit (80,000) is by counts.
        Document loose;
        emptyGroups(loose, 200);
        auto looseJson = QJsonDocument::fromJson(encodeDocument(loose)).object();
        check(decodeDocument(QJsonDocument(looseJson).toJson()).bodies().size() == 200,
              "A file of 200 loose groups loads");
        check(rejection([] {
                  checkLimits({0, DocumentLimits::unique.bodies + 1}, LimitClass::UniqueGeometry,
                              "Document load", {});
              }) == "Document load exceeds document unique geometry limits: bodies would be "
                    "80,001 of 80,000",
              "An oversized unique-geometry file is rejected on load, by name");

        // Unique vertices: one more vertex than the unique limit (2,400,000) is refused by name.
        Document vertices;
        for (int i = 0; i < 3; ++i)
            ring(vertices, i);
        const auto atVertexLimit = encodeDocument(vertices);
        check(decodeDocument(atVertexLimit).materializedTotals().vertices == 300,
              "A document of three 100-vertex faces loads");
        check(rejection([] {
                  checkLimits({0, 0, DocumentLimits::unique.vertices + 1}, LimitClass::UniqueGeometry,
                              "Document load", {});
              }) == "Document load exceeds document unique geometry limits: vertices would be "
                    "2,400,001 of 2,400,000",
              "An oversized unique-vertex file is rejected on load, by name");

        // A document chunk declared over the model bound (768 MiB) is refused by its range before
        // its bytes are read.
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
                  "A document chunk over the model bound is refused by its range");
        }

        // Component definitions: a stored definition over its vertex limit (800,000 at R115) is
        // refused on load, naming the definition.
        check(rejection([] {
                  checkLimits({0, 0, DocumentLimits::definition.vertices + 1},
                              LimitClass::ComponentDefinition, "Component definition",
                              definitionLimitContext("Edge", 1));
              }) == "Component definition exceeds component definition limits: vertices would be "
                    "800,001 of 800,000 (definition 'Edge' #1)",
              "A definition over 800,000 vertices is refused on load, by name");
        std::cout << "Document limits I/O: container save and reload, placement, unique body and "
                     "unique vertex load checks passed (counts at the R115 values)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
