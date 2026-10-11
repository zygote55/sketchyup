// R082.ee with R082.cc storage: a reopened instanced document still indexes each
// definition member once and answers inference exactly as before saving.
#include "core/components.hpp"
#include "core/groups.hpp"
#include "geometry/inference.hpp"
#include "io/document_io.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
InferenceCamera topCamera() {
    InferenceCamera c;
    const double s = .05; // Orthographic, 40 world units across.
    c.clipFromWorld = {s, 0, 0, 0, 0, s, 0, 0, 0, 0, -.01, 0, -.5, -.5, 0, 1};
    c.worldFromClip = {1 / s, 0, 0, 0, 0, 1 / s, 0, 0, 0, 0, -100, 0, 10, 10, 0, 1};
    c.width = 1000;
    c.height = 1000;
    return c;
}
} // namespace
int main() {
    try {
        Document doc;
        std::vector<Vec3> loop;
        for (int i = 0; i < 9; ++i) {
            const auto angle = 2 * std::numbers::pi * i / 9;
            loop.push_back({std::cos(angle), std::sin(angle), 0});
        }
        const auto body = doc.addFace({loop});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
        const auto leaf = createComponent(doc, body, "Leaf");
        std::vector<Id> placements{leaf.instance};
        for (int i = 1; i < 12; ++i)
            placements.push_back(
                placeComponent(doc, leaf.definition,
                               Transform::translation({3.0 * (i % 4), 3.0 * (i / 4), 0}) *
                                   (i % 2 ? Transform::scaling({-1, 1, 1})
                                          : Transform::rotation({0, 0, 1}, .2 * i)))
                    .instance);
        const auto assembly = createComponent(
            doc, createGroup(doc, {placements[10], placements[11]}), "Assembly");
        placeComponent(doc, assembly.definition, Transform::translation({0, 12, 0}));
        InferenceIndex original;
        original.sync(doc);
        const auto reopened = decodeContainer(encodeContainer(doc));
        InferenceIndex index;
        index.sync(reopened);
        check(index.primitiveCount() == original.primitiveCount(),
              "Reopened document exposes the same logical primitives");
        check(index.indexedPrimitiveCount() == original.indexedPrimitiveCount(),
              "Reopened instances still share their definition-local primitives (" +
                  std::to_string(index.indexedPrimitiveCount()) + " vs " +
                  std::to_string(original.indexedPrimitiveCount()) + ")");
        check(index.indexedPrimitiveCount() * 8 < index.primitiveCount(),
              "Instanced storage indexes members once");
        const auto camera = topCamera();
        size_t candidates = 0;
        for (int y = 40; y < 1000; y += 37)
            for (int x = 40; x < 1000; x += 37) {
                const InferenceQuery q{camera, double(x), double(y), 12};
                const auto a = original.query(q), b = index.query(q);
                check(a.candidates.size() == b.candidates.size() &&
                          a.visitedPrimitives == b.visitedPrimitives,
                      "Reopened query matches the original document");
                for (size_t i = 0; i < a.candidates.size(); ++i) {
                    const auto &l = a.candidates[i], &r = b.candidates[i];
                    check(l.kind == r.kind && l.point == r.point && l.body == r.body &&
                              l.entity == r.entity && l.otherBody == r.otherBody &&
                              l.otherEntity == r.otherEntity,
                          "Reopened candidate identity and point match exactly");
                }
                candidates += a.candidates.size();
            }
        check(candidates > 100, "Reopened queries acquire instance geometry");
        std::cout << "Instanced inference after reopen passed: logical="
                  << index.primitiveCount() << " stored=" << index.indexedPrimitiveCount()
                  << " candidates=" << candidates << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
