#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/sections.hpp"
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
    throw std::runtime_error("Expected section record rejection");
}
Id face(Document &doc) { return doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}}); }
} // namespace
int main() {
    try {
        Document doc;
        const auto body = face(doc), group = createGroup(doc, {body}, "Nested");
        const auto unrelated = face(doc);
        const auto originalGeometry = doc.bodies();
        doc.markSaved();
        const auto saved = doc.saveStamp();
        const auto root = createSection(doc, "Horizontal", 0, {{0, 0, 1}, -.5});
        const auto snapshot = doc.readSnapshot();
        check(doc.dirty() && snapshot.sections() == doc.sections() && !snapshot.canUndo(),
              "Section records participate in immutable snapshots and dirty state");
        doc.undo();
        check(doc.isCurrentSnapshot(saved) && doc.sections().empty() && doc.nextSectionId() > root,
              "Undo restores saved state while retaining allocator floor");
        doc.redo();
        const auto nested = createSection(doc, "Horizontal", group, {{1, 0, 0}, -1});
        setActiveSection(doc, 0, root);
        setActiveSection(doc, group, nested);
        check(effectiveSectionCuts(doc, body).size() == 2 &&
                  effectiveSectionCuts(doc, unrelated).size() == 1,
              "Nested scoped sections leave unrelated bodies unaffected");
        check(doc.bodies() == originalGeometry, "Section edits preserve authoritative geometry");
        const auto revision = doc.revision();
        updateSection(doc, nested, *doc.sections().at(nested));
        setActiveSection(doc, group, nested);
        check(doc.revision() == revision, "Equal section changes preserve history");
        rejects([&] { createSection(doc, "Horizontal", 0, {}); });
        rejects([&] { createSection(doc, "Unknown", 9999, {}); });
        rejects([&] { setActiveSection(doc, group, root); });
        check(doc.revision() == revision, "Invalid section operations are atomic");
        const auto parent = createGroup(doc, {group}, "Parent");
        doc.transform(parent, Transform::translation({3, 4, 5}) * Transform::scaling({-2, 3, 1}));
        doc.transform(group, Transform::rotation({0, 0, 1}, .3), parent);
        const auto cuts = effectiveSectionCuts(doc, body);
        const auto local = doc.worldTransform(group);
        check(std::abs(cuts[1].plane.distance(local.point({1, .2, .3}))) < 1e-7 &&
                  cuts[1].plane.distance(local.point({1.2, .2, .3})) > 0,
              "Nested mirrored context uses inverse-transpose plane placement");
        auto alias = std::make_shared<SectionRecord>(*doc.sections().at(nested));
        alias->name = "Frozen";
        Edit edit{"Freeze section", {}};
        edit.sections.push_back({nested, doc.sections().at(nested), alias});
        doc.apply(edit, doc.revision());
        alias->name = "Mutable";
        alias->plane.offset = 800;
        check(doc.sections().at(nested)->name == "Frozen" &&
                  doc.sections().at(nested)->plane.offset == -1,
              "Section publication freezes caller aliases");
        rejects([&] { doc.apply(edit, doc.revision()); });
        const auto proposal = doc.prepareEdit([&](Document &draft) {
            auto record = *draft.sections().at(nested);
            record.fill = false;
            updateSection(draft, nested, record);
        });
        check(doc.sections().at(nested)->fill, "Prepared section edit is isolated");
        doc.applyPrepared(proposal);
        check(!doc.sections().at(nested)->fill, "Prepared section edit commits");
        doc.undo();
        check(doc.sections().at(nested)->fill, "Prepared section Undo");
        eraseSection(doc, nested);
        check(!doc.activeSections().contains(group),
              "Deleting active plane deactivates its context");
        doc.undo();
        check(doc.activeSections().at(group) == nested, "Delete Undo restores activation");
        auto move = *doc.sections().at(nested);
        move.context = body;
        updateSection(doc, nested, move);
        check(!doc.activeSections().contains(group) && !doc.activeSections().contains(body),
              "Moving a plane between contexts deactivates without replacing destination");
        doc.undo();
        const auto stamp = doc.amendmentStamp();
        doc.amendLast(stamp, [&](Document &draft) {
            auto replacement = *draft.sections().at(nested);
            replacement.name = "Amended";
            updateSection(draft, nested, replacement);
        });
        doc.undo();
        doc.redo();
        doc.move(unrelated, {0, 0, 1});
        rejects([&] {
            doc.amendLast(doc.amendmentStamp(),
                          [&](Document &draft) { eraseSection(draft, root); });
        });
        setActiveSection(doc, group, {});
        rejects([&] {
            doc.amendLast(doc.amendmentStamp(),
                          [&](Document &draft) { setActiveSection(draft, 0, {}); });
        });
        const auto component = createComponent(doc, unrelated, "Shared");
        rejects([&] {
            editComponentDefinition(doc, component.definition, [&](Document &draft) {
                createSection(draft, "Shared section", 0, {});
                return ChangeReport{};
            });
        });
        doc.undo();
        // A raw context can be removed while its independent plane remains diagnosed.
        const auto orphan = createSection(doc, "Orphan", unrelated, {});
        setActiveSection(doc, unrelated, orphan);
        doc.erase(unrelated);
        check(missingSectionContexts(doc) == std::set<Id>{orphan} &&
                  doc.activeSections().at(unrelated) == orphan,
              "Missing contexts preserve records and inactive references");
        auto rename = *doc.sections().at(orphan);
        rename.name = "Missing context";
        updateSection(doc, orphan, rename);
        rejects([&] { setActiveSection(doc, unrelated, orphan); });
        Document allocator;
        const auto retired = createSection(allocator, "Retired", 0, {});
        allocator.undo();
        check(createSection(allocator, "New", 0, {}) > retired,
              "Section IDs cannot be recycled on another branch");
        Document bounded;
        const auto leaf = face(bounded);
        Id context = leaf;
        for (size_t i = 0; i < 8; ++i) {
            const auto plane = createSection(bounded, "Cut", context, {});
            setActiveSection(bounded, context, plane);
            context = createGroup(bounded, {context});
        }
        const auto excess = createSection(bounded, "Ninth", context, {});
        const auto before = bounded.revision();
        rejects([&] { setActiveSection(bounded, context, excess); });
        check(bounded.revision() == before && effectiveSectionCuts(bounded, leaf).size() == 8,
              "Active path bound rejects atomically");
        Document compound;
        auto candidate = compound.readSnapshot();
        const auto combined = createSection(candidate, "Compound", 0, {});
        setActiveSection(candidate, 0, combined);
        Edit together{"Create and activate section", {}};
        appendSceneMetadataChanges(together, compound, candidate);
        compound.apply(together, compound.revision());
        check(compound.sections().size() == 1 && compound.activeSections().at(0) == combined &&
                  compound.history().total == 1,
              "Compound section creation and activation publish once");
        compound.undo();
        check(compound.sections().empty() && compound.activeSections().empty(),
              "Compound Undo exact");
        compound.redo();
        auto restoreAlias = std::make_shared<SectionRecord>(*compound.sections().at(combined));
        Document restored;
        restored.restore(compound.identity(), 1, {}, 0, {}, {}, 1, {}, 1, {}, 1, {}, 1,
                         DisplayUnit::Meters, std::make_shared<const HostedComponents>(), {}, {}, 1,
                         {{combined, restoreAlias}}, compound.nextSectionId(), {{0, combined}});
        restoreAlias->name = "Mutated after restore";
        check(restored.sections().at(combined)->name == "Compound",
              "Restore freezes mutable records");
        check(restored.readSnapshotBytes() >
                  sectionBytes(restored.sections().at(combined)) + sizeof(Document),
              "Snapshot charges section and activation storage");
        Edit retiredReuse{"Reuse retired section", {}};
        retiredReuse.sections.push_back(
            {retired, nullptr,
             std::make_shared<SectionRecord>(SectionRecord{retired, "Reuse", 0, {}})});
        rejects([&] { allocator.apply(retiredReuse, allocator.revision()); });
        Document count;
        for (size_t i = 0; i < sectionRecordLimit; ++i)
            createSection(count, std::to_string(i), 0, {});
        rejects([&] { createSection(count, "Too many", 0, {}); });
        std::cout << "Section records, context state, history, isolation and bounds passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
