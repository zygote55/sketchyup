#include "core/assets.hpp"
#include "core/model.hpp"
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected rejection");
}
Edit paint(const Document &doc, const std::string &label, float red) {
    auto before = doc.bodies().at(1);
    auto after = std::make_shared<Body>(*before);
    after->color = {red, .2f, .3f};
    return {label, {{1, before, after}}};
}
int main() {
    try {
        Document doc;
        check(doc.history().entries.empty() && doc.history().baseSaved,
              "Empty history baseline is saved");
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        doc.markSaved();
        const auto saved = doc.bodies();
        auto task = paint(doc, "Tint window", .4f);
        task.metadata = {"task-window", "Tint both window assemblies", true};
        doc.apply(task, doc.revision());
        const auto stamp = doc.amendmentStamp();
        doc.amendLast(stamp, [](Document &draft) {
            draft.apply(paint(draft, "Replacement", .7f), draft.revision());
        });
        auto page = doc.history();
        check(page.total == 2 && page.position == 2 && page.entries[1].label == "Tint window" &&
                  page.entries[1].metadata == task.metadata && page.entries[0].saved,
              "Amendment preserves original label, AI request/task and saved marker");
        const auto amended = doc.bodies();
        doc.move(1, {1, 0, 0});
        const auto final = doc.bodies();
        auto revision = doc.revision();
        doc.navigateHistory(1, revision);
        check(doc.bodies() == saved && !doc.dirty() && doc.revision() == revision + 2 &&
                  doc.history().position == 1 && doc.history().total == 3,
              "Atomic multi-step rewind restores saved content and retains redo labels");
        page = doc.history(1, 1);
        check(page.entries.size() == 1 && page.entries[0].position == 2 &&
                  !page.entries[0].applied && page.entries[0].metadata.assistant,
              "Bounded chronological history page includes redo metadata");
        const auto undoState = doc.bodies();
        rejects([&] { doc.navigateHistory(3, revision); });
        rejects([&] { doc.navigateHistory(4, doc.revision()); });
        check(doc.bodies() == undoState && !doc.dirty(),
              "Stale and unavailable navigation are atomic failures");
        doc.navigateHistory(3, doc.revision());
        check(doc.bodies() == final && doc.dirty(), "Multi-step forward agrees with ordinary redo");
        doc.undo();
        check(doc.bodies() == amended, "Ordinary undo reaches the same labeled boundary");
        doc.redo();
        check(doc.bodies() == final, "Ordinary redo agrees with navigation");
        doc.navigateHistory(1, doc.revision());
        doc.paint(1, {.8f, .1f, .2f});
        check(doc.history().total == 2 && !doc.canRedo() &&
                  !doc.history().entries.back().metadata.assistant,
              "A new local edit discards only the redo branch and its task metadata");
        auto empty = paint(doc, "", .3f);
        rejects([&] { doc.apply(empty, doc.revision()); });
        auto invalid = paint(doc, "Invalid metadata", .3f);
        invalid.metadata.assistant = true;
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.metadata = {std::string(129, 'x'), "request", true};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.metadata = {"task", std::string(4097, 'x'), true};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.metadata = {std::string("a\0b", 3), "request", false};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        rejects([&] { doc.history(0, 0); });
        rejects([&] { doc.history(0, 1001); });
        rejects([&] { doc.history(100); });
        const auto noOpRevision = doc.revision();
        doc.navigateHistory(doc.history().position, noOpRevision);
        check(doc.revision() == noOpRevision, "Current history position is a no-op");
        Document overflow;
        overflow.restore(overflow.identity(), 1, {}, UINT64_MAX - 3);
        overflow.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        overflow.paint(1, {.2f, .3f, .4f});
        const auto overflowBodies = overflow.bodies();
        rejects([&] { overflow.navigateHistory(0, overflow.revision()); });
        check(overflow.bodies() == overflowBodies && overflow.history().position == 2,
              "Revision overflow is rejected before any partial rewind");
        Document bounded;
        bounded.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        for (size_t i = 0; i < Document::historyEntryLimit + 3; ++i)
            bounded.paint(1, {float(i % 2), .2f, .3f});
        check(bounded.history().total <= Document::historyEntryLimit && bounded.history().pruned &&
                  bounded.historyBytes() <= Document::historyLimit,
              "History entry count and memory are bounded with a visible pruned baseline");
        bounded.navigateHistory(0, bounded.revision());
        check(bounded.bodies().size() == 1 && !bounded.canUndo(),
              "Pruned history exposes only its reachable baseline");
        Document recovered;
        recovered.restore(doc.identity(), doc.nextId(), doc.bodies(), doc.revision());
        recovered.markRecovered();
        check(recovered.history().entries.empty() && !recovered.history().baseSaved &&
                  !recovered.history().pruned,
              "Restoration creates no history or saved marker for recovered content");
        std::cout
            << "Labeled history, task metadata, atomic navigation, saved markers, stale/overflow "
               "guards, amendments, redo branching and bounded retention passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
