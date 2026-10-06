#include "core/annotations.hpp"
#include "core/components.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message) { check(std::abs(a - b) < 1e-7, message); }
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid annotation edit accepted");
}
AnnotationRecord distance(const Document &doc, Id body, Id edge) {
    AnnotationRecord record;
    record.name = "Width";
    record.text = "Width";
    record.anchors = {edgeAnchor(doc, body, edge, .25), edgeAnchor(doc, body, edge, .75)};
    record.offset = {0, 1, 0};
    return record;
}
} // namespace
int main() {
    try {
        Document doc;
        const auto body = doc.addWire(0, {0, 0, 0}, {4, 0, 0});
        const auto edge = doc.bodies().at(body)->topology.edges.begin()->first;
        const auto geometry = doc.bodies();
        doc.markSaved();
        const auto saved = doc.saveStamp();
        const auto id = createAnnotation(doc, distance(doc, body, edge));
        const auto original = doc.annotations().at(id);
        check(doc.bodies() == geometry && doc.dirty(),
              "Annotation creates metadata without geometry changes");
        near(*measureAnnotation(doc, *original).distance, 2, "Distance measures world metres");
        doc.undo();
        check(doc.annotations().empty() && doc.isCurrentSnapshot(saved) &&
                  doc.nextAnnotationId() > id,
              "Undo restores saved state without reusing IDs");
        doc.redo();
        const auto revision = doc.revision();
        updateAnnotation(doc, id, *doc.annotations().at(id));
        check(doc.revision() == revision, "Equal annotation edit is a no-op");
        rejects([&] { createAnnotation(doc, distance(doc, body, edge)); });
        check(doc.revision() == revision, "Duplicate name rejection is atomic");
        const auto beforeScale = doc.annotations();
        doc.transform(body, Transform::scaling({-2, 3, 1}));
        near(*measureAnnotation(doc, *doc.annotations().at(id)).distance, 4,
             "Nonuniform reflected placement updates world length");
        doc.setDisplayUnits(DisplayUnit::FeetInches);
        near(*measureAnnotation(doc, *doc.annotations().at(id)).distance, 4,
             "Display units do not alter the stored metre measurement");
        doc.undo();
        doc.undo();
        check(doc.annotations() == beforeScale, "Scale and unit Undo restores exact anchors");
        AnnotationRecord label;
        label.name = "Split label";
        label.kind = AnnotationKind::Label;
        label.text = "Joint\nKeep clear";
        label.anchors = {edgeAnchor(doc, body, edge, .5)};
        const auto labelId = createAnnotation(doc, label);
        const auto beforeSplit = doc.annotations();
        const auto history = doc.history().total;
        doc.splitEdge(body, edge, .5);
        check(doc.history().total == history + 1,
              "Geometry and automatic attachments publish one history step");
        near(*measureAnnotation(doc, *doc.annotations().at(id)).distance, 2,
             "Split dimension stays measured");
        check(doc.annotations().at(labelId)->anchors[0].state == AnchorState::Ambiguous,
              "Split-boundary label stores explicit ambiguity");
        doc.undo();
        check(doc.annotations() == beforeSplit,
              "Undo restores exact annotation snapshots with geometry");
        doc.redo();
        auto rename = *doc.annotations().at(labelId);
        rename.text = "Ambiguous joint";
        updateAnnotation(doc, labelId, rename);
        check(measureAnnotation(doc, *doc.annotations().at(labelId)).state ==
                  AnchorState::Ambiguous,
              "Broken label stays editable and diagnosed");
        const auto beforeMove = doc.annotations();
        doc.move(body, {0, 2, 0});
        near(measureAnnotation(doc, *doc.annotations().at(id)).anchors[0].point.y, 2,
             "Move updates attached point");
        const auto stamp = doc.amendmentStamp();
        doc.amendLast(stamp, [&](Document &draft) { draft.move(body, {0, 3, 0}); });
        near(measureAnnotation(doc, *doc.annotations().at(id)).anchors[0].point.y, 3,
             "Numeric amendment replaces attachment movement");
        doc.undo();
        check(doc.annotations() == beforeMove,
              "Amended Undo restores geometry and attachments exactly");
        doc.redo();
        const auto prepared =
            doc.prepareEdit([&](Document &draft) { draft.move(body, {0, 1, 0}); });
        near(measureAnnotation(doc, *doc.annotations().at(id)).anchors[0].point.y, 3,
             "Prepared change remains isolated");
        doc.applyPrepared(prepared);
        near(measureAnnotation(doc, *doc.annotations().at(id)).anchors[0].point.y, 4,
             "Prepared publication does not remap twice");
        const auto beforeDelete = doc.annotations();
        doc.erase(body);
        const auto broken = measureAnnotation(doc, *doc.annotations().at(id));
        check(broken.state == AnchorState::Missing && !broken.distance,
              "Deleted geometry invalidates measurement instead of reporting stale value");
        near(broken.anchors[0].point.y, 4, "Broken marker retains last resolved world position");
        doc.undo();
        check(doc.annotations() == beforeDelete,
              "Geometry deletion Undo restores annotation values");
        auto alias = std::make_shared<AnnotationRecord>(*doc.annotations().at(id));
        alias->text = "Frozen";
        Edit edit{"Update label", {}};
        edit.annotations.push_back({id, doc.annotations().at(id), alias});
        doc.apply(edit, doc.revision());
        alias->text = "Mutable";
        check(doc.annotations().at(id)->text == "Frozen", "Publication freezes aliases");
        rejects([&] { doc.apply(edit, doc.revision()); });
        const auto snapshot = doc.readSnapshot();
        check(snapshot.annotations() == doc.annotations() && !snapshot.canUndo(),
              "Read snapshot owns immutable annotations");
        eraseAnnotation(doc, id);
        doc.undo();
        check(doc.annotations().at(id)->text == "Frozen", "Delete annotation Undo exact");
        auto invalid = *doc.annotations().at(id);
        invalid.anchors[0].entity = 999999;
        rejects([&] { updateAnnotation(doc, id, invalid); });
        invalid = *doc.annotations().at(id);
        invalid.textSize = 100;
        rejects([&] { updateAnnotation(doc, id, invalid); });
        Document compound;
        auto draft = compound.readSnapshot();
        AnnotationRecord fixed;
        fixed.name = "Fixed span";
        fixed.anchors = {pointAnchor({}), pointAnchor({3, 4, 0})};
        const auto fixedId = createAnnotation(draft, fixed);
        auto named = *draft.annotations().at(fixedId);
        named.text = "Diagonal";
        updateAnnotation(draft, fixedId, named);
        Edit together{"Create named dimension", {}};
        appendSceneMetadataChanges(together, compound, draft);
        compound.apply(together, compound.revision());
        check(compound.history().total == 1, "Composed metadata publishes once");
        near(*measureAnnotation(compound, *compound.annotations().at(fixedId)).distance, 5,
             "Fixed-point dimension measures Euclidean distance");
        compound.undo();
        check(compound.annotations().empty(), "Compound Undo exact");
        compound.redo();
        const auto part = doc.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
        const auto component = createComponent(doc, part, "Part");
        rejects([&] {
            editComponentDefinition(doc, component.definition, [&](Document &scope) {
                createAnnotation(scope, fixed);
                return ChangeReport{};
            });
        });
        std::cout << "Annotation records, associations, atomic history, prepared edits, amendments "
                     "and scope passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
