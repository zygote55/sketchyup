#include "core/model.hpp"
#include "core/selection.hpp"
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        Document live(DisplayUnit::Millimeters);
        const auto body = live.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        live.extrude(body, 5, 1);
        live.markSaved();
        live.move(body, {2, 0, 0});
        live.undo();
        check(live.canUndo() && live.canRedo() && !live.dirty(),
              "Snapshot fixture requires retained history and saved baseline");
        Selection editor;
        editor.apply(live, {{body, SelectionKind::Body, 0}}, SelectionMode::Replace);
        auto captured = live.readSnapshot();
        const auto stamp = live.saveStamp();
        check(captured.readSnapshotBytes() == live.readSnapshotBytes() &&
                  captured.readSnapshotBytes() > sizeof(Document),
              "Snapshot charge must include current records and omit history");
        check(captured.identity() == live.identity() && captured.revision() == live.revision() &&
                  captured.displayUnits() == DisplayUnit::Millimeters && !captured.dirty() &&
                  captured.isCurrentSnapshot(stamp) && editor.belongsTo(captured),
              "Snapshot must preserve exact identity, revision, editor session and saved marker");
        check(!captured.canUndo() && !captured.canRedo() && captured.historyBytes() == 0 &&
                  captured.history().total == 0,
              "Read snapshot must not retain undo/redo entries");
        check(captured.bodies().at(body) == live.bodies().at(body),
              "Snapshot should share immutable scene records");
        live.redo();
        live.setDisplayUnits(DisplayUnit::FeetInches);
        check(captured.bodies().at(body) != live.bodies().at(body) && !captured.dirty() &&
                  captured.displayUnits() == DisplayUnit::Millimeters &&
                  captured.isCurrentSnapshot(stamp),
              "Live edits must not change captured geometry, units or state");
        const auto dirty = live.readSnapshot();
        check(dirty.dirty() && dirty.historyBytes() == 0,
              "Dirty capture must remain dirty without retaining history");
        const auto original = *captured.bodies().at(body);
        live.erase(body);
        check(*captured.bodies().at(body) == original,
              "Live deletion must not retire retained snapshot geometry");
        // A returned value remains an ordinary document, so its allocator floors
        // must be safe even when used as a future private editing candidate.
        const auto retired = live.addFace({{{3, 0, 0}, {4, 0, 0}, {4, 1, 0}, {3, 1, 0}}});
        live.undo();
        auto fork = live.readSnapshot();
        const auto liveNext = live.nextId();
        const auto created = fork.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}});
        check(created >= liveNext && created > retired && live.bodies().empty() &&
                  !live.isCurrentSnapshot(fork.saveStamp()),
              "Private snapshot edits must preserve floors and leave live state untouched");
        {
            auto callerBody = std::make_shared<Body>(*captured.bodies().at(body));
            const auto expected = *callerBody;
            Document restored;
            restored.restore(captured.identity(), captured.nextId(), {{body, callerBody}});
            check(restored.bodies().at(body).get() != callerBody.get(),
                  "Public restore must freeze caller-owned bodies");
            callerBody->name = "Caller mutation after restore";
            callerBody->surface.vertices.begin()->second.x += 10;
            check(*restored.bodies().at(body) == expected,
                  "Caller mutation cannot alter a restored document");

            auto legacy = std::make_shared<Body>(expected);
            legacy->topology = {};
            Document upgraded;
            upgraded.restore(captured.identity(), captured.nextId(), {{body, legacy}});
            check(legacy->topology.edges.empty() &&
                      !upgraded.bodies().at(body)->topology.edges.empty(),
                  "Legacy topology rebuilding must not mutate caller records");
            const auto before = upgraded.bodies().at(body);
            const auto identity = upgraded.identity();
            bool rejected = false;
            try {
                upgraded.restore(identity, captured.nextId() + 1,
                                 {{body, legacy}, {captured.nextId(), nullptr}});
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected && upgraded.identity() == identity &&
                      upgraded.bodies().at(body) == before,
                  "Rejected restore must preserve the previously published document");
        }
        std::cout << "Immutable read snapshots preserve records, identity, floors and saved state "
                     "without history\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
