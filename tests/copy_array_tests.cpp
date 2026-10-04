#include "core/copy_array.hpp"
#include <climits>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected array rejection");
}
Document square() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    return doc;
}
int main() {
    try {
        auto doc = square();
        const auto source = doc.bodies().at(1);
        CopyArray linear{ArrayMode::Linear, {2, 0, 0}, {0, 0, 1}, 0, 3, false};
        auto result = copyArraySelected(doc, {{1, TransformKind::Face, 5}}, linear);
        check(doc.bodies().size() == 1 && doc.bodies().at(1)->surface.faces.size() == 4 &&
                  result.instances.size() == 3,
              "Count denotes new copies inside source context");
        for (unsigned i = 0; i < 3; ++i) {
            const auto vertex = result.instances[i].geometryCopies.at(1).vertices.at(1);
            check(doc.bodies().at(1)->surface.vertices.at(vertex) == Vec3{2. * (i + 1), 0, 0},
                  "Linear copy spacing is exact");
        }
        check(result.changes.at(1).faces.descendants.at(5).size() == 4,
              "Lineage includes source and every copy");
        const auto floor = doc.bodies().at(1)->surface.nextId;
        doc.undo();
        check(doc.bodies().at(1)->surface.vertices == source->surface.vertices &&
                  doc.bodies().at(1)->surface.faces == source->surface.faces,
              "One undo removes whole array");
        linear.divide = true;
        result = copyArraySelected(doc, {{1, TransformKind::Face, 5}}, linear);
        for (unsigned i = 0; i < 3; ++i) {
            const auto vertex = result.instances[i].geometryCopies.at(1).vertices.at(1);
            check(vertex >= floor && length(doc.bodies().at(1)->surface.vertices.at(vertex) -
                                            Vec3{2. * (i + 1) / 3, 0, 0}) < tolerance,
                  "Equal divisions include endpoint and never reuse retired IDs");
        }
        doc = square();
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        doc.transform(2, Transform::translation({3, 0, 0}), 1);
        CopyArray radial{ArrayMode::Radial, {}, {0, 0, 1}, std::numbers::pi / 2, 3, false};
        result = copyArraySelected(doc, {{1, TransformKind::Context, 0}}, radial);
        check(doc.bodies().size() == 8 && result.instances.size() == 3,
              "Radial hierarchy count includes descendant contexts");
        for (unsigned i = 0; i < 3; ++i) {
            const auto child = result.instances[i].copies.at(2);
            const auto root = result.instances[i].copies.at(1);
            const auto expected =
                Transform::rotation({0, 0, 1}, (i + 1) * std::numbers::pi / 2).point({3, 0, 0});
            check(doc.bodies().at(child)->parent == root &&
                      length(doc.worldTransform(child).point({}) - expected) < tolerance,
                  "Radial copies preserve hierarchy and exact angle step");
        }
        doc.undo();
        check(doc.bodies().size() == 2, "Hierarchy array has one undo");
        auto amendDoc = square();
        amendDoc.addFace({{{3, 0, 0}, {4, 0, 0}, {4, 1, 0}, {3, 1, 0}}});
        const auto neighbor = amendDoc.bodies().at(2);
        radial.copies = 1;
        copyArraySelected(amendDoc, {{1, TransformKind::Context, 0}}, radial);
        const auto stamp = amendDoc.amendmentStamp();
        const auto originalCopy = amendDoc.bodies().rbegin()->first;
        radial.copies = 3;
        rejects([&] {
            amendDoc.amendLast(stamp, [&](Document &candidate) {
                copyArraySelected(candidate, {{1, TransformKind::Context, 0}}, radial);
            });
        });
        check(amendDoc.bodies().size() == 3 && amendDoc.bodies().contains(originalCopy),
              "Default amendment still rejects creation count changes");
        rejects([&] {
            amendDoc.amendLast(
                stamp, [&](Document &candidate) { candidate.move(2, {1, 0, 0}); },
                Document::AmendPolicy::CopyArray);
        });
        check(amendDoc.bodies().at(2) == neighbor,
              "Array amendment cannot change another editing context");
        amendDoc.amendLast(
            stamp,
            [&](Document &candidate) {
                copyArraySelected(candidate, {{1, TransformKind::Context, 0}}, radial);
            },
            Document::AmendPolicy::CopyArray);
        check(amendDoc.bodies().size() == 5 && !amendDoc.bodies().contains(originalCopy),
              "Array amendment replaces one copied context with three fresh copies");
        amendDoc.undo();
        check(amendDoc.bodies().size() == 2 && amendDoc.bodies().at(2) == neighbor,
              "Changed context count still shares one undo item");
        auto local = square();
        local.transform(1, Transform::rotation({0, 0, 1}, std::numbers::pi / 2));
        linear.divide = false;
        result = copyArraySelected(local, {{1, TransformKind::Context, 0}}, linear, {},
                                   TransformSpace::Local);
        check(length(local.worldTransform(result.instances[0].copies.at(1)).point({}) -
                     Vec3{0, 2, 0}) < tolerance,
              "Local arrays follow rotated context axes");
        // Counts and aggregate budgets reject before publishing any candidate.
        auto invalid = square();
        const auto before = invalid.bodies().at(1);
        const auto revision = invalid.revision(), next = invalid.nextId();
        for (auto count : {0u, 101u, 1001u, UINT_MAX}) {
            linear.copies = count;
            rejects([&] { copyArraySelected(invalid, {{1, TransformKind::Face, 5}}, linear); });
        }
        linear.copies = 2;
        linear.delta = {coordinateLimit, 0, 0};
        rejects([&] { copyArraySelected(invalid, {{1, TransformKind::Face, 5}}, linear); });
        radial.axis = {};
        rejects([&] { copyArraySelected(invalid, {{1, TransformKind::Face, 5}}, radial); });
        check(invalid.revision() == revision && invalid.nextId() == next &&
                  invalid.bodies().at(1) == before,
              "Invalid array leaves all authoritative state unchanged");
        auto guides = square();
        for (int i = 0; i < 11; ++i)
            guides.addGuide(1, guidePoint({double(i), 0, 0}));
        TransformTargets selected;
        for (const auto &[id, guide] : guides.bodies().at(1)->guides)
            selected.insert({1, TransformKind::Guide, id});
        const auto guidesBefore = guides.bodies().at(1);
        linear = {ArrayMode::Linear, {2, 0, 0}, {0, 0, 1}, 0, maxArrayCopies, false};
        rejects([&] { copyArraySelected(guides, selected, linear); });
        check(guides.bodies().at(1) == guidesBefore,
              "Aggregate per-context guide budget rejects expansion atomically");
        std::cout << "Linear/radial arrays, divisions, counts, hierarchy, local axes, lineage, "
                     "undo and bounded rejection passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
