#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/text_commands.hpp"
#include "core/annotations.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid text command accepted");
}
QJsonObject create() {
    return {{"command", "text.create"}, {"name", "Sign"}, {"text", "O"},
            {"family", "DejaVu Sans"},  {"height", .2},   {"depth", .03},
            {"allowSubstitution", true}};
}
QJsonObject batch(Document &doc, QJsonArray commands) {
    return executeBatch(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", commands}});
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto created = batch(doc, {create()});
        check(created["created"].toArray() == QJsonArray{"1"},
              "Text creation exposes stable body identity");
        const auto old = doc.bodies().at(1);
        check(old->kind == BodyKind::Group && old->textSource && !old->surface.faces.empty(),
              "Text has editable source and native cached geometry");
        check(textDescription(doc, 1)["geometryMatchesSource"] == true,
              "Generated geometry matches source digest");
        AnnotationRecord annotation;
        annotation.name = "Text reference";
        annotation.kind = AnnotationKind::Label;
        annotation.text = "Top";
        annotation.anchors = {vertexAnchor(doc, 1, old->surface.vertices.begin()->first)};
        const auto annotationId = createAnnotation(doc, annotation);
        batch(doc, {QJsonObject{{"command", "text.update"}, {"body", "1"}, {"text", "B\ncafé"}}});
        check(doc.bodies().at(1)->textSource->text == "B\ncafé" &&
                  doc.annotations().at(annotationId)->anchors[0].state == AnchorState::Missing,
              "Regeneration replaces source and invalidates retired glyph references");
        check(doc.bodies().at(1)->surface.vertices.begin()->first >= old->surface.nextId,
              "Regenerated glyphs use fresh geometry identities");
        doc.undo();
        check(doc.bodies().at(1)->textSource == old->textSource &&
                  doc.bodies().at(1)->surface.vertices == old->surface.vertices &&
                  doc.annotations().at(annotationId)->anchors[0].state == AnchorState::Resolved,
              "One Undo restores source, cached geometry and attached annotation");
        doc.redo();
        const auto stable = encodeContainer(doc);
        rejects([&] {
            batch(doc, {QJsonObject{{"command", "text.update"},
                                    {"body", "1"},
                                    {"family", "Missing font cb85"},
                                    {"allowSubstitution", false}}});
        });
        check(encodeContainer(doc) == stable, "Missing font failure is atomic");
        rejects([&] {
            batch(doc, {QJsonObject{{"command", "text.update"}, {"body", "1"}, {"text", "A"}},
                        QJsonObject{{"command", "text.bake"}, {"body", "999999"}}});
        });
        check(encodeContainer(doc) == stable, "A later failure rolls back generated geometry");
        auto before = doc.bodies().at(1);
        auto changed = std::make_shared<Body>(*before);
        changed->textSource->fonts.front().fingerprint = std::string(64, '0');
        doc.apply({"Simulate font revision", {{1, before, changed}}}, doc.revision());
        const auto fontRevision = encodeContainer(doc);
        rejects([&] {
            batch(doc,
                  {QJsonObject{{"command", "text.update"}, {"body", "1"}, {"regenerate", true}}});
        });
        check(encodeContainer(doc) == fontRevision, "Changed font requires explicit acceptance");
        batch(doc, {QJsonObject{{"command", "text.update"},
                                {"body", "1"},
                                {"regenerate", true},
                                {"acceptFontChange", true}}});
        check(doc.bodies().at(1)->textSource->fonts.front().fingerprint != std::string(64, '0'),
              "Accepted font change refreshes actual provenance");
        auto placement = Transform::translation({2, 3, 4});
        placement.m[0] = -2;
        before = doc.bodies().at(1);
        changed = std::make_shared<Body>(*before);
        changed->transform = placement;
        doc.apply({"Place text", {{1, before, changed}}}, doc.revision());
        batch(doc, {QJsonObject{{"command", "text.update"}, {"body", "1"}, {"text", "i"}}});
        check(doc.bodies().at(1)->transform == placement, "Editing preserves reflected placement");
        before = doc.bodies().at(1);
        changed = std::make_shared<Body>(*before);
        changed->surface.translate({.1, 0, 0});
        doc.apply({"Manual geometry edit", {{1, before, changed}}}, doc.revision());
        check(textDescription(doc, 1)["geometryMatchesSource"] == false,
              "Independent shape changes are visible");
        const auto modified = encodeContainer(doc);
        rejects([&] {
            batch(doc, {QJsonObject{{"command", "text.update"}, {"body", "1"}, {"text", "X"}}});
        });
        check(encodeContainer(doc) == modified,
              "Regeneration does not overwrite independent edits");
        batch(doc, {QJsonObject{{"command", "text.update"}, {"body", "1"}, {"name", "Renamed"}}});
        const auto shape = doc.bodies().at(1)->surface;
        batch(doc, {QJsonObject{{"command", "text.bake"}, {"body", "1"}}});
        check(!doc.bodies().at(1)->textSource && doc.bodies().at(1)->surface == shape,
              "Baking removes source and keeps exact authored geometry");
        doc.undo();
        check(doc.bodies().at(1)->textSource.has_value(), "Bake is undoable");
        setEntityState(doc, 1, {}, true);
        rejects([&] { batch(doc, {QJsonObject{{"command", "text.bake"}, {"body", "1"}}}); });
        doc.undo();
        const auto stored = encodeContainer(doc);
        check(encodeContainer(decodeContainer(stored)) == stored,
              "Command-authored source reopens exactly");
        Document componentDoc;
        batch(componentDoc, {create()});
        const auto component = createComponent(componentDoc, 1, "Sign component");
        const auto member = component.movedGeometry.at(1);
        batch(componentDoc,
              {QJsonObject{{"command", "component.edit_instance"},
                           {"body", "1"},
                           {"commands", QJsonArray{QJsonObject{{"command", "text.update"},
                                                               {"body", QString::number(member)},
                                                               {"text", "B"}}}}}});
        bool found{};
        for (const auto &[_, b] : componentDoc.bodies())
            if (b->textSource) {
                check(b->textSource->text == "B", "Instance edit updates text source");
                found = true;
            }
        check(found, "Component retains editable text");
        std::cout << "Text creation, regeneration, font policy, baking, lineage, rollback and "
                     "components passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
