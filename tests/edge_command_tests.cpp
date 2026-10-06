#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/edge_appearance.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected command rejection");
}
QString sid(Id id) { return QString::number(id); }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", sid(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(Id body, Id edge, QJsonObject flags, Id context = 0) {
    flags["command"] = "geometry.edge_appearance";
    flags["context"] = sid(context);
    flags["entities"] = QJsonArray{QJsonObject{{"body", sid(body)}, {"edge", sid(edge)}}};
    return flags;
}
Id square(Document &doc, double x = 0) {
    return doc.addFace({{{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 2, 0}, {x, 2, 0}}});
}
QJsonObject inspect(const Document &doc, QString query, QJsonObject arguments) {
    arguments["apiVersion"] = 1;
    arguments["documentId"] = QString::fromStdString(doc.identity());
    arguments["expectedRevision"] = sid(doc.revision());
    arguments["query"] = query;
    return inspectDocument(doc, arguments)["data"].toObject();
}
void basic() {
    Document doc;
    const auto body = square(doc), edge = doc.bodies().at(body)->topology.edges.begin()->first;
    const auto original = doc.bodies().at(body);
    const auto bytes = encodeContainer(doc);
    const auto depth = doc.history().total;
    const auto request =
        batch(doc, {command(body, edge, {{"hidden", true}, {"soft", true}, {"smooth", true}})});
    const auto preview = previewBatch(doc, request);
    check(encodeContainer(doc) == bytes, "Preview is immutable");
    const auto result = executeBatch(doc, request);
    check(result["changes"] == preview["changes"] && doc.history().total == depth + 1,
          "Preview predicts one committed edit");
    const auto styled = doc.bodies().at(body);
    check(styled->surface == original->surface && styled->topology == original->topology &&
              edgeAppearance(*styled, edge) == EdgeAppearance{true, true, true},
          "Command changes only flags");
    const auto detail =
        inspect(doc, "entity.describe", {{"target", inspectionReference(doc, body, "edge", edge)}});
    check(detail["edgeAppearance"] ==
              QJsonObject{{"hidden", true}, {"soft", true}, {"smooth", true}},
          "Edge detail exposes flags");
    const auto listed = inspect(doc, "topology.query",
                                {{"target", inspectionReference(doc, body)},
                                 {"kind", "edge"},
                                 {"space", "local"},
                                 {"limit", 100}})["items"]
                            .toArray();
    bool found = false;
    for (auto value : listed) {
        auto row = value.toObject();
        if (row["ref"].toObject()["id"] == sid(edge)) {
            found = true;
            check(row["appearance"] == detail["edgeAppearance"], "Paged edge flags agree");
        }
    }
    check(found, "Styled edge appears in topology query");
    doc.undo();
    check(*doc.bodies().at(body) == *original, "Undo exact original body");
    doc.redo();
    check(*doc.bodies().at(body) == *styled, "Redo exact styled body");
    executeBatch(doc, batch(doc, {command(body, edge, {{"hidden", false}})}));
    check(edgeAppearance(*doc.bodies().at(body), edge) == EdgeAppearance{false, true, true},
          "Unspecified flags retained");
    check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
          "Command appearance persists");
    auto good = command(body, edge, {{"smooth", false}});
    auto noFlags = command(body, edge, {}), noContext = good, duplicate = good, missing = good,
         extra = good;
    noContext.remove("context");
    duplicate["entities"] =
        QJsonArray{good["entities"].toArray()[0], good["entities"].toArray()[0]};
    missing["entities"] = QJsonArray{QJsonObject{{"body", sid(body)}, {"edge", "999"}}};
    extra["unknown"] = true;
    const auto before = encodeContainer(doc);
    for (const auto &bad : QJsonArray{noFlags, noContext, duplicate, missing, extra,
                                      command(body, edge, {{"hidden", 1}}),
                                      command(body, edge, {{"smooth", QJsonValue::Null}})})
        rejects([&] { executeBatch(doc, batch(doc, {bad})); });
    rejects([&] { executeBatch(doc, request); });
    rejects(
        [&] { executeBatch(doc, batch(doc, {good, QJsonObject{{"command", "not_a_command"}}})); });
    check(encodeContainer(doc) == before, "Invalid/stale/late batches are atomic");
    setEntityState(doc, body, {}, true);
    rejects([&] { executeBatch(doc, batch(doc, {good})); });
}
void compound() {
    Document doc;
    const auto body = square(doc), edge = doc.bodies().at(body)->topology.edges.begin()->first;
    executeBatch(doc, batch(doc, {command(body, edge, {{"hidden", true}, {"smooth", true}})}));
    auto prediction = doc.readSnapshot();
    const auto split = prediction.splitEdge(body, edge, .5);
    const auto children = split.at(body).edges.descendants.at(edge);
    QJsonArray actions{QJsonObject{{"command", "geometry.split_edge"},
                                   {"body", sid(body)},
                                   {"edge", sid(edge)},
                                   {"fraction", .5}}};
    for (auto child : children)
        actions.append(command(body, child, {{"hidden", false}, {"smooth", false}}));
    const auto initial = doc.bodies().at(body);
    const auto request = batch(doc, actions);
    const auto preview = previewBatch(doc, request);
    const auto result = executeBatch(doc, request);
    check(preview["changes"] == result["changes"] && doc.bodies().at(body)->edgeAppearances.empty(),
          "Split then clear stays cleared through batch composition");
    doc.undo();
    check(doc.bodies().at(body)->edgeAppearances == initial->edgeAppearances,
          "Compound Undo restores original flags");
    Document pair;
    const auto a = square(pair), b = square(pair, 2);
    auto seam = [](const Body &body) {
        for (const auto &[id, e] : body.topology.edges) {
            auto x = body.surface.vertices.at(e.a), y = body.surface.vertices.at(e.b);
            if (x.x == 2 && y.x == 2)
                return id;
        }
        throw std::runtime_error("Missing seam");
    };
    const auto ae = seam(*pair.bodies().at(a)), be = seam(*pair.bodies().at(b));
    executeBatch(pair, batch(pair, {command(a, ae, {{"soft", true}})}));
    auto staged = pair.readSnapshot();
    setEdgeAppearance(staged, {{b, SelectionKind::Edge, be}}, 0, {}, true, {});
    const auto merged = consolidateContext(staged);
    const auto target = merged.transfers.at(b).edges.at(be);
    const auto revision = pair.revision();
    executeBatch(pair,
                 batch(pair, {command(b, be, {{"soft", true}}),
                              QJsonObject{{"command", "geometry.merge_context"}, {"context", "0"}},
                              command(merged.destination, target, {{"soft", false}})}));
    check(pair.revision() == revision + 1 && pair.bodies().size() == 1 &&
              pair.bodies().at(merged.destination)->edgeAppearances.empty(),
          "Unify then merge then clear keeps authoritative final metadata");
    pair.undo();
    check(pair.bodies().size() == 2 && edgeAppearance(*pair.bodies().at(a), ae).soft &&
              !edgeAppearance(*pair.bodies().at(b), be).soft,
          "Compound merge Undo restores distinct source flags");
}
void componentScope() {
    Document doc;
    const auto body = square(doc), edge = doc.bodies().at(body)->topology.edges.begin()->first;
    const auto group = createGroup(doc, {body});
    const auto component = createComponent(doc, group);
    const auto other = placeComponent(doc, component.definition, Transform::scaling({-1, 1, 1}));
    executeBatch(
        doc, batch(doc, {componentScopeCommand(
                            doc, component.instance,
                            {command(body, edge, {{"hidden", true}, {"smooth", true}}, group)})}));
    for (const auto &[id, b] : doc.bodies())
        if (!b->surface.faces.empty())
            check(edgeAppearance(*b, edge) == EdgeAppearance{true, false, true},
                  "Shared scoped edit updates reflected instances");
    makeComponentUnique(doc, component.instance);
    const auto untouched = doc.instances().at(other.instance);
    const auto before = doc.bodies();
    executeBatch(
        doc,
        batch(doc, {componentScopeCommand(doc, component.instance,
                                          {command(body, edge, {{"hidden", false}}, group)})}));
    for (const auto &[member, scene] : untouched->members)
        check(doc.bodies().at(scene) == before.at(scene),
              "Unique edit preserves other instance records");
    check(!edgeAppearance(*doc.bodies().at(body), edge).hidden, "Unique target changes");
    check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
          "Scoped command persists");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        basic();
        compound();
        componentScope();
        std::cout
            << "Edge appearance commands, inspection, batches, scope and persistence passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
