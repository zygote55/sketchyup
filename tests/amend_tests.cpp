#include "core/model.hpp"
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
Id rectangle(Document &doc, double width) {
    return doc.addFace({{{0, 0, 0}, {width, 0, 0}, {width, 1, 0}, {0, 1, 0}}});
}
int main() {
    try {
        Document doc;
        const auto neighbor = rectangle(doc, 2);
        const auto untouched = doc.bodies().at(neighbor);
        doc.markSaved();
        const auto original = rectangle(doc, 3);
        const auto stamp = doc.amendmentStamp();
        auto savedOld = doc.saveStamp();
        const auto revision = doc.revision();
        const auto next = doc.nextId();
        auto preview = doc;
        Id replacement = 0;
        preview.amendLast(stamp,
                          [&](Document &candidate) { replacement = rectangle(candidate, 4); });
        check(doc.bodies().contains(original) && doc.revision() == revision,
              "Amend candidate leaves source unchanged");
        const auto report = doc.amendLast(
            stamp, [&](Document &candidate) { replacement = rectangle(candidate, 4); });
        check(replacement >= next && !doc.bodies().contains(original) &&
                  doc.bodies().contains(replacement),
              "Replacement allocates fresh identity");
        check(doc.revision() == revision + 1 && !doc.canAmend(stamp),
              "Replacement increments one revision and expires old token");
        check(doc.bodies().at(neighbor) == untouched &&
                  doc.bodies()
                          .at(replacement)
                          ->surface.area(
                              doc.bodies().at(replacement)->surface.faces.begin()->first) == 4,
              "Replacement changes only requested geometry");
        check(report.contains(original) && report.contains(replacement),
              "Replacement reports retired and new contexts");
        doc.markSaved(savedOld);
        check(doc.dirty(), "Old save completion cannot mark replacement clean");
        doc.undo();
        check(doc.bodies().size() == 1 && doc.bodies().at(neighbor)->surface == untouched->surface,
              "One undo removes replacement and original operation");
        doc.redo();
        check(doc.bodies().contains(replacement), "Redo restores replacement IDs");
        auto amend = doc.amendmentStamp();
        auto before = doc.bodies().at(replacement);
        const auto history = doc.historyBytes();
        auto oldRevision = doc.revision();
        rejects(
            [&] { doc.amendLast(amend, [&](Document &candidate) { rectangle(candidate, 0); }); });
        check(doc.bodies().at(replacement) == before && doc.revision() == oldRevision &&
                  doc.historyBytes() == history,
              "Invalid amendment preserves document and history");
        rejects([&] {
            doc.amendLast(amend,
                          [&](Document &candidate) { candidate.paint(neighbor, {1, 0, 0}); });
        });
        check(doc.bodies().at(neighbor) == untouched && doc.bodies().at(replacement) == before,
              "Amendment cannot edit another context");
        doc.paint(neighbor, {.1f, .2f, .3f});
        rejects(
            [&] { doc.amendLast(amend, [&](Document &candidate) { rectangle(candidate, 5); }); });
        auto afterPaint = doc.amendmentStamp();
        doc.undo();
        doc.redo();
        check(!doc.canAmend(afterPaint), "Undo/redo invalidates re-entry despite restored content");
        Document other;
        rectangle(other, 1);
        check(!other.canAmend(doc.amendmentStamp()), "Token cannot cross document session");
        Document solid;
        auto body = rectangle(solid, 4);
        auto face = solid.bodies().at(body)->surface.faces.begin()->first;
        solid.pushPull(body, face, 2);
        auto extrusion = solid.amendmentStamp();
        const auto oldSurface = solid.bodies().at(body)->surface;
        const auto floor = oldSurface.nextId;
        solid.amendLast(extrusion, [&](Document &candidate) { candidate.pushPull(body, face, 3); });
        check(solid.bodies().at(body)->surface.nextId > floor &&
                  solid.bodies().at(body)->surface.faces.contains(face),
              "Push/pull amendment preserves base and advances floors");
        double volume = 0;
        for (const auto &triangle : solid.bodies().at(body)->surface.triangles())
            volume += dot(triangle.a, cross(triangle.b, triangle.c)) / 6;
        check(std::abs(volume - 12) < 1e-8,
              "Amended extrusion replaces distance instead of adding it");
        solid.undo();
        check(solid.bodies().at(body)->surface.faces.size() == 1,
              "One undo returns to unextruded profile");
        solid.redo();
        std::cout << "Atomic amendments, context guards, stale tokens, one-step history and "
                     "allocator floors passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
