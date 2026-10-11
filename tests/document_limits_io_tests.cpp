// R082.gg native bounds at the document limits: a document at the materialized
// body and placement limits saves and reloads within the 4 MiB manifest bound,
// and over-limit files are rejected on load with bounded D06 diagnostics.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include "io/native_limits.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
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
        std::cout << "Document limits I/O: 40,000-body save/reload, placement and unique "
                     "load rejections passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
