#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/edge_appearance.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected edge appearance rejection");
}
Id square(Document &doc, double x = 0) {
    return doc.addFace({{{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 2, 0}, {x, 2, 0}}});
}
void style(Document &doc, Id body, Id edge, EdgeAppearance flags, Id context = 0) {
    setEdgeAppearance(doc, {{body, SelectionKind::Edge, edge}}, context, flags.hidden, flags.soft,
                      flags.smooth);
}
Id edgeAt(const Body &body, Vec3 a, Vec3 b) {
    for (const auto &[id, edge] : body.topology.edges) {
        const auto x = body.surface.vertices.at(edge.a), y = body.surface.vertices.at(edge.b);
        if ((x == a && y == b) || (x == b && y == a))
            return id;
    }
    throw std::runtime_error("Fixture edge missing");
}
void flagsAndHistory() {
    Document doc;
    const auto body = square(doc), edge = doc.bodies().at(body)->topology.edges.begin()->first;
    const auto initial = doc.bodies().at(body);
    const auto depth = doc.history().total;
    const auto report =
        setEdgeAppearance(doc, {{body, SelectionKind::Edge, edge}}, 0, true, {}, {});
    check(edgeAppearance(*doc.bodies().at(body), edge) == EdgeAppearance{true, false, false} &&
              report.at(body).edges.modified == std::vector<Id>{edge},
          "Appearance-only edit reports modified edge");
    setEdgeAppearance(doc, {{body, SelectionKind::Edge, edge}}, 0, {}, true, true);
    const auto changed = doc.bodies().at(body);
    check(changed->surface == initial->surface && changed->topology == initial->topology &&
              changed->edgeAppearances.at(edge) == EdgeAppearance{true, true, true},
          "Independent flags preserve geometry, triangulation and allocators");
    check(doc.history().total == depth + 2, "Each edit has one history item");
    check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
          "Container flags roundtrip exactly");
    const auto snapshot = doc.prepareEdit([&](Document &draft) { style(draft, body, edge, {}); });
    check(doc.bodies().at(body) == changed &&
              snapshot.snapshot().bodies().at(body)->edgeAppearances.empty(),
          "Prepared appearance edit is private");
    doc.applyPrepared(snapshot);
    check(doc.bodies().at(body)->edgeAppearances.empty(), "All-false removes sparse override");
    doc.undo();
    check(*doc.bodies().at(body) == *changed, "Undo restores flags exactly");
    doc.redo();
    check(doc.bodies().at(body)->edgeAppearances.empty(), "Redo clears flags");
    const auto unchanged = encodeContainer(doc);
    check(setEdgeAppearance(doc, {{body, SelectionKind::Edge, edge}}, 0, false, false, false)
                  .empty() &&
              encodeContainer(doc) == unchanged,
          "Repeated default request has no edit");
    rejects([&] { setEdgeAppearance(doc, {}, 0, true, {}, {}); });
    rejects([&] { setEdgeAppearance(doc, {{body, SelectionKind::Face, 5}}, 0, true, {}, {}); });
    rejects([&] { setEdgeAppearance(doc, {{body, SelectionKind::Edge, edge}}, 0, {}, {}, {}); });
    rejects([&] { style(doc, body, 999, {true, false, false}); });
    check(encodeContainer(doc) == unchanged, "Invalid edits are atomic");
    setEntityState(doc, body, {}, true);
    rejects([&] { style(doc, body, edge, {true, false, false}); });
    setEntityState(doc, body, {}, false);
    const auto group = createGroup(doc, {body});
    rejects([&] { style(doc, body, edge, {true, false, false}); });
    style(doc, body, edge, {true, false, false}, group);
}
void splitsAndCopies() {
    Document doc;
    const auto body = square(doc), edge = edgeAt(*doc.bodies().at(body), {0, 0, 0}, {2, 0, 0});
    const EdgeAppearance flags{true, true, true};
    style(doc, body, edge, flags);
    const auto original = doc.bodies().at(body);
    const auto split = doc.splitEdge(body, edge, .5);
    const auto children = split.at(body).edges.descendants.at(edge);
    check(children.size() == 2 && !doc.bodies().at(body)->edgeAppearances.contains(edge),
          "Split retires old style identity");
    for (auto child : children)
        check(edgeAppearance(*doc.bodies().at(body), child) == flags,
              "Both split children inherit flags");
    doc.undo();
    check(doc.bodies().at(body)->edgeAppearances == original->edgeAppearances,
          "Split Undo restores style");
    doc.redo();
    const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
    const auto before = doc.bodies().at(body);
    const auto copied =
        transformSelected(doc, {{body, TransformKind::Face, face}},
                          Transform::translation({4, 0, 0}), {}, TransformSpace::World, true);
    for (auto child : children)
        check(edgeAppearance(*doc.bodies().at(body),
                             copied.geometryCopies.at(body).edges.at(child)) == flags,
              "Raw face copy transfers edge flags to fresh IDs");
    reverseSelectedFaces(doc, {{body, SelectionKind::Face, face}}, 0);
    for (auto child : children)
        check(edgeAppearance(*doc.bodies().at(body), child) == flags,
              "Reversal retains edge flags");
    const auto prepared = doc.prepareEdit([&](Document &candidate) {
        auto draft = candidate.readSnapshot();
        const auto result = draft.splitEdge(body, children.front(), .5);
        for (auto next : result.at(body).edges.descendants.at(children.front()))
            style(draft, body, next, {});
        Change composed{body,
                        candidate.bodies().at(body),
                        draft.bodies().at(body),
                        {},
                        {},
                        result.at(body).edges.descendants,
                        true};
        candidate.apply({"Split then clear", {composed}}, candidate.revision());
    });
    const auto expected = prepared.snapshot().bodies().at(body)->edgeAppearances;
    doc.applyPrepared(prepared);
    check(doc.bodies().at(body)->edgeAppearances == expected,
          "Composed split then clear never resurrects inherited flags");
    // Erasing one boundary retires its style and the incident face, without dangling keys.
    const auto remaining = children.back();
    doc.eraseEdge(body, remaining);
    check(!doc.bodies().at(body)->edgeAppearances.contains(remaining),
          "Deleted edge style is pruned");
    validateEdgeAppearances(*doc.bodies().at(body));
}
void mergingAndGrouping() {
    Document doc;
    const auto a = square(doc), b = square(doc, 2);
    const auto ae = edgeAt(*doc.bodies().at(a), {2, 0, 0}, {2, 2, 0}),
               be = edgeAt(*doc.bodies().at(b), {2, 0, 0}, {2, 2, 0});
    const EdgeAppearance flags{false, true, true};
    style(doc, a, ae, flags);
    const auto old = encodeContainer(doc);
    rejects([&] { consolidateContext(doc); });
    check(encodeContainer(doc) == old, "Conflicting seam flags reject merge atomically");
    style(doc, b, be, flags);
    const auto merged = consolidateContext(doc);
    const auto seam = merged.transfers.at(b).edges.at(be);
    check(edgeAppearance(*doc.bodies().at(a), seam) == flags && doc.bodies().size() == 1,
          "Aligned seam flags survive cross-body welding");
    Selection selected;
    const auto face = merged.transfers.at(b).faces.begin()->second;
    selected.apply(doc, {{a, SelectionKind::Face, face}}, SelectionMode::Replace);
    const auto group = groupSelected(doc, selected);
    const auto moved = group.movedGeometry.at(a);
    check(edgeAppearance(*doc.bodies().at(moved), seam) == flags,
          "Partial grouping keeps moved edge flags");
    check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
          "Grouped appearances reopen");
}
void cleanupConflicts() {
    Document doc;
    auto wire = std::make_shared<Body>();
    wire->id = 1;
    wire->surface.vertices = {{1, {0, 0, 0}}, {2, {2, 0, 0}}, {3, {0, 0, 0}}, {4, {2, 0, 0}}};
    wire->surface.wires = {{1, 2}, {3, 4}};
    wire->surface.nextId = 5;
    doc.apply({"Coincident wires", {{1, nullptr, wire}}}, doc.revision());
    const auto first = doc.bodies().at(1)->topology.edges.begin()->first;
    const auto last = doc.bodies().at(1)->topology.edges.rbegin()->first;
    check(first != last, "Two independently identified wire edges");
    style(doc, 1, first, {true, false, false});
    const auto before = encodeContainer(doc);
    rejects([&] { doc.cleanup(1); });
    check(encodeContainer(doc) == before,
          "Default source conflicts with retained styled edge during cleanup");
    style(doc, 1, last, {true, false, false});
    const auto cleaned = doc.cleanup(1);
    check(doc.bodies().at(1)->topology.edges.size() == 1 &&
              edgeAppearance(*doc.bodies().at(1), first) == EdgeAppearance{true, false, false} &&
              cleaned.at(1).edges.descendants.at(last) == std::vector<Id>{first},
          "Matching coincident wire appearances merge to retained identity");
}
void componentsAndMalformed() {
    Document doc;
    const auto body = square(doc), edge = doc.bodies().at(body)->topology.edges.begin()->first;
    style(doc, body, edge, {true, false, true});
    const auto group = createGroup(doc, {body});
    const auto component = createComponent(doc, group);
    const auto other = placeComponent(doc, component.definition, Transform::scaling({-1, 1, 1}));
    const auto definition = doc.definitions().at(component.definition);
    Id member{};
    for (const auto &[id, b] : definition->members)
        if (!b->surface.faces.empty())
            member = id;
    editComponentDefinition(doc, component.definition, [&](Document &draft) {
        return setEdgeAppearance(draft, {{member, SelectionKind::Edge, edge}}, definition->root, {},
                                 true, {});
    });
    for (auto root : {component.instance, other.instance}) {
        const auto scene = doc.instances().at(root)->members.at(member);
        check(edgeAppearance(*doc.bodies().at(scene), edge) == EdgeAppearance{true, true, true},
              "Definition edit updates all mirrored instances");
    }
    const auto bytes = encodeContainer(doc);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Canonical and scene flags persist together");
    doc.undo();
    doc.redo();
    check(encodeContainer(doc) != QByteArray{}, "Component Undo/Redo remains valid");
    Document plain;
    const auto id = square(plain), e = plain.bodies().at(id)->topology.edges.begin()->first;
    style(plain, id, e, {false, false, true});
    auto root = QJsonDocument::fromJson(encodeDocument(plain)).object();
    auto bodies = root["bodies"].toArray();
    auto record = bodies[0].toObject();
    const auto original = record;
    for (const auto &bad : QJsonArray{
             QJsonObject{{"hidden", true}, {"soft", 0}, {"smooth", false}},
             QJsonObject{{"hidden", false}, {"soft", false}, {"smooth", false}},
             QJsonObject{{"hidden", true}, {"soft", true}, {"smooth", true}, {"extra", true}}}) {
        record["edgeAppearances"] = QJsonObject{{QString::number(e), bad}};
        bodies[0] = record;
        root["bodies"] = bodies;
        rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
    }
    record = original;
    record["edgeAppearances"] =
        QJsonObject{{"999", QJsonObject{{"hidden", true}, {"soft", false}, {"smooth", false}}}};
    bodies[0] = record;
    root["bodies"] = bodies;
    rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
    // v12 migration has hard, visible, unsmoothed edges and never invents flags.
    record = original;
    record.remove("edgeAppearances");
            record.remove("faceTextureMappings");
    bodies[0] = record;
    root["bodies"] = bodies;
    root["version"] = 12;
    root.remove("style");
    root.remove("scenes");
    root.remove("sections");
    root.remove("nextSectionId");
    root.remove("activeSections");
    root.remove("nextSceneId");
    root.remove("hosted");
    check(decodeDocument(QJsonDocument(root).toJson()).bodies().at(id)->edgeAppearances.empty(),
          "Historical document defaults preserve old display semantics");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        flagsAndHistory();
        splitsAndCopies();
        mergingAndGrouping();
        cleanupConflicts();
        componentsAndMalformed();
        std::cout << "Persistent edge appearance, lineage, components, history and codecs passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
