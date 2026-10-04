#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/groups.hpp"
#include "core/tags.hpp"
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *message) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
Id square(Document &doc) { return doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}); }
int main() {
    try {
        Document doc;
        const auto body = square(doc);
        const auto folder = createTag(doc, "Architecture", 0, true);
        const auto tag = createTag(doc, "Walls", folder);
        assignTag(doc, body, tag);
        const auto geometry = doc.bodies();
        const auto pose = doc.worldTransform(body);
        editTag(doc, folder, {}, {}, false);
        Selection selection;
        check(selection.hidden(doc, {body, SelectionKind::Body, 0}) && doc.bodies() == geometry &&
                  doc.worldTransform(body) == pose,
              "Folder visibility changes neither geometry records nor ownership or transforms");
        doc.undo();
        check(tagVisible(doc.tags(), tag), "Folder visibility is undoable");
        doc.redo();
        editTag(doc, tag, {}, 0);
        check(tagVisible(doc.tags(), tag) && doc.bodies() == geometry,
              "Moving a tag between folders changes organization, not model parentage");
        const auto snapshot = doc.tags();
        const auto revision = doc.revision();
        rejects([&] { editTag(doc, folder, {}, folder); }, "Folder cycles reject");
        rejects([&] { editTag(doc, folder, {}, tag); }, "Tags cannot parent folders");
        rejects([&] { createTag(doc, "Walls"); }, "Duplicate sibling names reject");
        rejects([&] { eraseTag(doc, tag); }, "Used tags cannot be deleted");
        rejects([&] { assignTag(doc, body, folder); }, "Folders cannot be assigned as tags");
        check(doc.tags() == snapshot && doc.revision() == revision, "Invalid tag edits are atomic");
        const auto child = createTag(doc, "Details", folder, true);
        rejects([&] { editTag(doc, folder, {}, child); }, "Multi-node folder cycles reject");
        rejects([&] { eraseTag(doc, folder); }, "Nonempty folders cannot be deleted");
        const auto transient = createTag(doc, "Transient");
        doc.undo();
        const auto fresh = createTag(doc, "Fresh");
        check(fresh > transient, "Undone tag IDs are not reused");
        const auto recordId = doc.nextTagId();
        auto mutableRecord =
            std::make_shared<TagRecord>(TagRecord{recordId, 0, "Frozen", false, true});
        Edit frozen{"Tag snapshot", {}};
        frozen.tags.push_back({recordId, nullptr, mutableRecord});
        doc.apply(frozen, doc.revision());
        mutableRecord->name = "Mutated";
        check(doc.tags().at(recordId)->name == "Frozen",
              "Caller-owned tag records are frozen at publication");
        rejects([&] { doc.apply(frozen, doc.revision()); }, "Stale tag records reject");
        const auto original = doc.tags().at(recordId);
        auto changedKind = std::make_shared<TagRecord>(*original);
        changedKind->folder = true;
        Edit changed{"Change tag kind", {}};
        changed.tags.push_back({recordId, original, changedKind});
        rejects([&] { doc.apply(changed, doc.revision()); }, "Tag identities cannot change kind");
        const auto component = createComponent(doc, body, "Tagged wall");
        const auto member = component.movedGeometry.at(body);
        const auto peer = placeComponent(doc, component.definition).instance;
        check(doc.bodies().at(body)->tag == tag && !doc.bodies().at(member)->tag &&
                  !doc.bodies().at(peer)->tag,
              "Component creation retains placement tag while fresh placements start Untagged");
        editTag(doc, tag, {}, {}, false);
        check(selection.hidden(doc, {member, SelectionKind::Face, 5}) &&
                  !selection.hidden(doc, {peer, SelectionKind::Body, 0}),
              "Placement tags affect only that instance and its contents");
        editTag(doc, tag, {}, {}, true);
        editComponentDefinition(doc, component.definition,
                                [&](Document &draft) { return assignTag(draft, member, tag); });
        const auto peerMember = doc.instances().at(peer)->members.at(member);
        check(doc.bodies().at(peerMember)->tag == tag,
              "Canonical member tag assignment propagates to all peers");
        const auto beforeScope = doc.tags();
        rejects(
            [&] {
                editComponentDefinition(doc, component.definition, [&](Document &draft) {
                    editTag(draft, tag, "Renamed");
                    return ChangeReport{};
                });
            },
            "Global tag metadata cannot mutate inside a shared geometry scope");
        check(doc.tags() == beforeScope,
              "Rejected shared tag-table changes leave document tags intact");
        Document merge;
        const auto a = square(merge), b = square(merge);
        const auto category = createTag(merge, "Separate");
        assignTag(merge, b, category);
        check(consolidationGroups(merge, 0).size() == 2,
              "Merge partitions preserve independent tag assignments");
        rejects([&] { consolidateContext(merge); },
                "A single merged record cannot erase differing tags");
        Document history;
        const auto historyBody = square(history);
        const auto historyTag = createTag(history, "Initial");
        const auto otherTag = createTag(history, "Other");
        editTag(history, historyTag, "First rename");
        const auto stamp = history.amendmentStamp();
        rejects(
            [&] {
                history.amendLast(stamp,
                                  [&](Document &draft) { editTag(draft, otherTag, "Unrelated"); });
            },
            "Tag amendment cannot change an unrelated tag");
        history.amendLast(stamp,
                          [&](Document &draft) { editTag(draft, historyTag, "Final rename"); });
        check(history.tags().at(historyTag)->name == "Final rename",
              "Tag amendment publishes the replacement");
        history.undo();
        check(history.tags().at(historyTag)->name == "Initial",
              "Amended tag edits retain one-step undo");
        history.redo();
        assignTag(history, historyBody, historyTag);
        setEntityState(history, historyBody, {}, true);
        const auto lockedBodies = history.bodies();
        rejects([&] { assignTag(history, historyBody, otherTag); },
                "Locked entity tag assignments cannot change");
        editTag(history, historyTag, {}, {}, false);
        check(history.bodies() == lockedBodies &&
                  selection.hidden(history, {historyBody, SelectionKind::Body, 0}),
              "Tag visibility can hide locked entities without changing their records");
        TagRecords excessive;
        for (Id id = 1; id <= 1025; ++id)
            excessive[id] =
                std::make_shared<TagRecord>(TagRecord{id, 0, std::to_string(id), false, true});
        rejects([&] { validateTagRecords(excessive, 1026); }, "Tag count is bounded");
        excessive.clear();
        for (Id id = 1; id <= 33; ++id)
            excessive[id] =
                std::make_shared<TagRecord>(TagRecord{id, id - 1, std::to_string(id), true, true});
        rejects([&] { validateTagRecords(excessive, 34); }, "Tag folder depth is bounded");
        std::cout << "Tag folders, independent visibility, history, identity, component scope and "
                     "merge partitions passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
