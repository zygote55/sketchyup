#include "core/components.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/tags.hpp"
#include <iostream>
#include <limits>
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
    throw std::runtime_error("Expected scene rejection");
}
SceneSnapshot camera(double yaw = -45) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->yaw = yaw;
    return snapshot;
}
} // namespace
int main() {
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        SceneSnapshot all = camera();
        all.style = doc.style();
        all.section = SceneSection{};
        all.visibility =
            SceneVisibility{{{body, true}}, {}, {{body, SceneEntityKind::Face, face}}, true};
        doc.markSaved();
        const auto saved = doc.saveStamp();
        const auto first = createScene(doc, "Perspective", all);
        const auto frozen = doc.readSnapshot();
        check(doc.dirty() && frozen.scenes() == doc.scenes() && !frozen.canUndo(),
              "Scene snapshot/history");
        check(doc.readSnapshotBytes() > sizeof(Document) + sceneBytes(doc.scenes().at(first)),
              "Snapshot accounts scene storage");
        doc.undo();
        check(doc.scenes().empty() && doc.isCurrentSnapshot(saved),
              "Scene Undo returns saved state");
        check(doc.nextSceneId() > first && frozen.scenes().size() == 1,
              "Undo preserves floor and snapshot");
        doc.redo();
        const auto second = createScene(doc, "Top", camera(0));
        const auto third = createScene(doc, "Back", camera(180));
        reorderScenes(doc, {third, first, second});
        check(orderedScenes(doc) == std::vector<Id>{third, first, second},
              "Stable identities and explicit order");
        eraseScene(doc, first);
        check(orderedScenes(doc) == std::vector<Id>{third, second}, "Deletion compacts order");
        doc.undo();
        check(orderedScenes(doc) == std::vector<Id>{third, first, second},
              "Deletion Undo restores exact order");
        const auto revision = doc.revision();
        renameScene(doc, first, "Perspective");
        updateScene(doc, first, all);
        reorderScenes(doc, orderedScenes(doc));
        check(doc.revision() == revision, "Equal operations do not dirty history");
        for (const auto &name : {"", " Perspective", "Perspective ", "Top", "bad\nname"})
            rejects([&] { renameScene(doc, first, name); });
        rejects([&] { reorderScenes(doc, {first, first, third}); });
        rejects([&] { reorderScenes(doc, {first, second, 999}); });
        rejects([&] { reorderScenes(doc, {first}); });
        rejects([&] { updateScene(doc, first, {}); });
        check(doc.revision() == revision, "Rejected operations leave state untouched");
        auto alias = std::make_shared<SceneRecord>(*doc.scenes().at(first));
        alias->name = "Frozen";
        Edit change{"Freeze scene", {}};
        change.scenes.push_back({first, doc.scenes().at(first), alias});
        doc.apply(change, doc.revision());
        alias->name = "Mutated alias";
        alias->snapshot.camera->yaw = 17;
        check(doc.scenes().at(first)->name == "Frozen" && doc.scenes().at(first)->snapshot == all,
              "Caller aliases cannot mutate published scenes");
        rejects([&] { doc.apply(change, doc.revision()); });
        const auto amendment = doc.amendmentStamp();
        doc.amendLast(amendment, [&](Document &draft) { renameScene(draft, first, "Amended"); });
        doc.undo();
        check(doc.scenes().at(first)->name == "Perspective",
              "Scene amendment retains original before");
        doc.redo();
        const auto proposal =
            doc.prepareEdit([&](Document &draft) { updateScene(draft, first, camera(90)); });
        check(doc.scenes().at(first)->snapshot == all, "Prepared scene isolated");
        doc.applyPrepared(proposal);
        doc.undo();
        check(doc.scenes().at(first)->snapshot == all, "Prepared scene Undo");
        doc.move(body, {1, 0, 0});
        rejects([&] {
            doc.amendLast(doc.amendmentStamp(),
                          [&](Document &draft) { renameScene(draft, first, "Outside"); });
        });
        const auto component = createComponent(doc, body, "Component");
        rejects([&] {
            editComponentDefinition(doc, component.definition, [&](Document &draft) {
                renameScene(draft, first, "Illegal shared scene");
                return ChangeReport{};
            });
        });
        doc.undo();
        doc.erase(body);
        const auto missing = missingSceneReferences(doc, doc.scenes().at(first)->snapshot);
        check(missing.bodies.contains(body) &&
                  missing.entities.contains({body, SceneEntityKind::Face, face}),
              "Deleted references diagnosed, not erased");
        renameScene(doc, first, "Missing geometry");
        reorderScenes(doc, {second, third, first});
        rejects([&] { createScene(doc, "Invalid capture", all); });
        rejects([&] { updateScene(doc, first, all); });
        check(doc.scenes().at(first)->snapshot == all, "Metadata edits retain missing references");
        for (int variant = 0; variant < 13; ++variant) {
            auto bad = camera();
            if (variant == 0)
                bad.camera->yaw = 181;
            if (variant == 1)
                bad.camera->pitch = -91;
            if (variant == 2)
                bad.camera->distance = .049;
            if (variant == 3)
                bad.camera->distance = 1e8;
            if (variant == 4)
                bad.camera->fieldOfView = 121;
            if (variant == 5)
                bad.camera->target.x = std::numeric_limits<double>::quiet_NaN();
            if (variant == 6)
                bad.camera->target.z = 1e10;
            if (variant == 7)
                bad.section = SceneSection{{{0, 0, 0, 0}}};
            if (variant == 8)
                bad.section = SceneSection{{{0, 0, 2, 0}}};
            if (variant == 9)
                bad.visibility = SceneVisibility{{{0, true}}, {}, {}, false};
            if (variant == 10)
                bad.visibility = SceneVisibility{{}, {{0, true}}, {}, false};
            if (variant == 11)
                bad.visibility =
                    SceneVisibility{{{1, true}}, {}, {{1, SceneEntityKind::Face, 0}}, false};
            if (variant == 12)
                bad.style = ModelStyle{}, bad.style->xrayOpacity = 1;
            rejects([&] { createScene(doc, "Bad", bad); });
        }
        Document floor;
        const auto retired = createScene(floor, "Retired", camera());
        floor.undo();
        const auto next = createScene(floor, "New", camera());
        check(next > retired, "New branch cannot recycle retired scene IDs");
        Edit reused{"Reuse retired", {}};
        reused.scenes.push_back(
            {retired, nullptr,
             std::make_shared<SceneRecord>(SceneRecord{retired, "Reuse", 1, camera()})});
        rejects([&] { floor.apply(reused, floor.revision()); });
        auto restored = floor.readSnapshot();
        auto mutableRecord = std::make_shared<SceneRecord>(*floor.scenes().at(next));
        restored.restore(floor.identity(), 1, {}, 0, {}, {}, 1, {}, 1, {}, 1, {}, 1,
                         DisplayUnit::Meters, std::make_shared<const HostedComponents>(), {},
                         {{next, mutableRecord}}, floor.nextSceneId());
        mutableRecord->name = "Alias";
        check(restored.scenes().at(next)->name == "New", "Restore freezes records");
        Document bounded;
        for (size_t i = 0; i < sceneCountLimit; ++i)
            createScene(bounded, std::to_string(i), camera());
        rejects([&] { createScene(bounded, "Over budget", camera()); });
        check(bounded.scenes().size() == sceneCountLimit, "Scene count bound atomic");
        SceneRecords oversized;
        SceneVisibility many;
        for (Id i = 1; i <= 10000; ++i)
            many.bodyVisible[i] = true;
        for (Id i = 1; i <= 15; ++i) {
            auto snapshot = camera();
            snapshot.visibility = many;
            oversized[i] = std::make_shared<SceneRecord>(SceneRecord{
                i, std::to_string(i), static_cast<std::uint32_t>(i - 1), std::move(snapshot)});
        }
        rejects([&] { validateSceneRecords(oversized, 16); });
        const auto beforeRestore = restored.saveStamp();
        rejects([&] {
            restored.restore(restored.identity(), 1, {}, 0, {}, {}, 1, {}, 1, {}, 1, {}, 1,
                             DisplayUnit::Meters, std::make_shared<const HostedComponents>(), {},
                             oversized, 16);
        });
        check(restored.isCurrentSnapshot(beforeRestore), "Oversized restore is atomic");
        auto composedDraft = floor.readSnapshot();
        renameScene(composedDraft, next, "Compound");
        Edit composed{"Compound scene metadata", {}};
        appendSceneMetadataChanges(composed, floor, composedDraft);
        floor.apply(composed, floor.revision());
        check(floor.scenes().at(next)->name == "Compound", "Compound publication includes scenes");
        Document sectionDoc;
        const auto scopedBody = sectionDoc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}});
        const auto rootPlane = createSection(sectionDoc, "Root", 0, {});
        const auto bodyPlane = createSection(sectionDoc, "Body", scopedBody, {{1, 0, 0}, -.5});
        setActiveSection(sectionDoc, 0, rootPlane);
        setActiveSection(sectionDoc, scopedBody, bodyPlane);
        SceneSnapshot sectionSnapshot;
        sectionSnapshot.section = SceneSection{std::nullopt, sectionDoc.activeSections()};
        sectionSnapshot.style = sectionDoc.style();
        sectionSnapshot.style->mode = ModelStyleMode::Monochrome;
        const auto sectionScene = createScene(sectionDoc, "Cut view", sectionSnapshot);
        const auto sectionGeometry = sectionDoc.bodies();
        setActiveSection(sectionDoc, 0, std::nullopt);
        setActiveSection(sectionDoc, scopedBody, std::nullopt);
        const auto beforeRecall = sectionDoc.saveStamp();
        recallSceneModel(sectionDoc, sectionScene);
        check(sectionDoc.activeSections() == sectionSnapshot.section->active &&
                  sectionDoc.style() == *sectionSnapshot.style && sectionDoc.bodies() == sectionGeometry,
              "Scene recall restores named activation and style without changing geometry");
        sectionDoc.undo();
        check(sectionDoc.activeSections().empty() && sectionDoc.isCurrentSnapshot(beforeRecall),
              "One Undo restores all recalled persistent properties");
        sectionDoc.redo();
        check(!sceneRecallChangesModel(sceneRecallEdit(sectionDoc, sectionScene)),
              "Equal named section recall is a no-op");
        SceneSnapshot off;
        off.section = SceneSection{};
        const auto offScene = createScene(sectionDoc, "Cuts off", off);
        recallSceneModel(sectionDoc, offScene);
        check(sectionDoc.activeSections().empty(), "Empty captured activation turns named cuts off");
        sectionDoc.undo();
        eraseSection(sectionDoc, rootPlane);
        auto missingSections = missingSceneReferences(sectionDoc, sectionSnapshot);
        check(missingSections.sections == std::set<Id>{rootPlane} && missingSections.size() == 1,
              "Deleted section references are retained and diagnosed");
        rejects([&] { createScene(sectionDoc, "Invalid capture", sectionSnapshot); });
        recallSceneModel(sectionDoc, sectionScene);
        check(sectionDoc.activeSections() == ActiveSections{{scopedBody, bodyPlane}},
              "Recall skips a deleted section without inventing a replacement");
        auto moved = *sectionDoc.sections().at(bodyPlane);
        moved.context = 0;
        updateSection(sectionDoc, bodyPlane, moved);
        check(missingSceneReferences(sectionDoc, sectionSnapshot).sections ==
                  std::set<Id>{rootPlane, bodyPlane},
              "Relocated sections cannot silently change saved scene context");
        SceneSection malformed;
        malformed.active = {{0, 1}, {scopedBody, 1}};
        rejects([&] { malformed.validate(); });
        malformed.active = {{0, 0}};
        rejects([&] { malformed.validate(); });
        std::cout << "Scene validation, history, ordering, references, proposals, scope and bounds "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
