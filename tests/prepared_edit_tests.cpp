#include "core/model.hpp"
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected, "Expected prepared edit rejection");
}
int main() {
    try {
        Document live;
        auto body = live.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        live.markSaved();
        const auto stamp = live.saveStamp();
        const auto revision = live.revision();
        const auto records = live.bodies();
        const auto history = live.history().total;
        int calls = 0;
        auto proposal = live.prepareEdit([&](Document &draft) {
            check(!draft.canUndo() && !draft.canRedo(), "Private operation must omit live history");
            ++calls;
            draft.move(body, {2, 0, 0});
        });
        check(calls == 1 && live.revision() == revision && live.isCurrentSnapshot(stamp) &&
                  !live.dirty() && live.bodies() == records && live.history().total == history,
              "Preparing must not mutate live state, history or saved marker");
        check(proposal.baseRevision() == revision &&
                  proposal.snapshot().revision() == revision + 1 &&
                  !proposal.snapshot().canUndo() && proposal.retainedBytes() > sizeof(Document) &&
                  proposal.snapshot().worldTransform(body).point({0, 0, 0}) == Vec3{2, 0, 0},
              "Prepared snapshot must contain exact proposed state without history");
        auto foreign = Document();
        check(!foreign.canApply(proposal), "Foreign document must reject proposal");
        rejects([&] { foreign.applyPrepared(proposal); });
        // Prebuild publication privately: disk persistence can precede moving this value into live.
        auto publication = live;
        publication.applyPrepared(proposal);
        check(live.isCurrentSnapshot(stamp) && publication.history().total == history + 1 &&
                  publication.history().entries.back().label == "Move",
              "Publication candidate must preserve prior history and add one proposed entry");
        live.applyPrepared(proposal);
        check(calls == 1 && live.revision() == revision + 1 &&
                  live.history().total == history + 1 &&
                  live.worldTransform(body).point({0, 0, 0}) == Vec3{2, 0, 0},
              "Commit must apply the proposal once without rerunning the operation");
        rejects([&] { live.applyPrepared(proposal); });
        live.undo();
        check(!live.dirty() && !live.canApply(proposal),
              "Undo to the base still invalidates proposal");
        live.redo();
        check(live.worldTransform(body).point({0, 0, 0}) == Vec3{2, 0, 0},
              "Undo/redo geometry survives");
        const auto current = live.saveStamp();
        rejects([&] { live.prepareEdit([](Document &) {}); });
        rejects([&] {
            live.prepareEdit([&](Document &d) {
                d.move(body, {1, 0, 0});
                d.move(body, {1, 0, 0});
            });
        });
        rejects([&] {
            live.prepareEdit([&](Document &d) {
                d.move(body, {1, 0, 0});
                throw std::runtime_error("fail");
            });
        });
        rejects([&] { live.prepareEdit([](Document &d) { d = Document(); }); });
        check(live.isCurrentSnapshot(current), "Failed preparation cannot publish changes");
        auto unit =
            live.prepareEdit([](Document &d) { d.setDisplayUnits(DisplayUnit::Millimeters); });
        live.markSaved();
        check(live.canApply(unit), "Saving unchanged content must not invalidate a proposal");
        live.applyPrepared(unit);
        check(live.displayUnits() == DisplayUnit::Millimeters && live.dirty(),
              "Metadata proposal applies");
        live.undo();
        check(!live.dirty(), "Commit must preserve the current saved marker");
        auto stale = live.prepareEdit([&](Document &d) { d.move(body, {0, 1, 0}); });
        live.move(body, {0, 0, 1});
        rejects([&] { live.applyPrepared(stale); });
        check(stale.snapshot().worldTransform(body).point({0, 0, 0}) == Vec3{2, 1, 0},
              "Live edits cannot alter retained proposal geometry");
        std::cout << "Private prepared edits preserve history, immutable previews and strict CAS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
