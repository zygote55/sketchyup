#include "core/appearance.hpp"
#include "core/groups.hpp"
#include "core/transform_selection.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void tint(Document &doc, Id body, Id face, std::array<float, 3> color) {
    const auto old = doc.bodies().at(body);
    auto next = std::make_shared<Body>(*old);
    next->faceColors[face] = color;
    doc.apply({"Tint fixture", {{body, old, next}}}, doc.revision());
}
} // namespace
int main() {
    try {
        const std::array<float, 3> red{.8f, .1f, .2f}, blue{.1f, .2f, .8f};
        Document doc;
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        tint(doc, 1, 5, red);
        doc.insertEdges(1, {}, {0, 0, 1}, {{Vec3{2, 0, 0}, Vec3{2, 4, 0}}});
        for (const auto &[face, record] : doc.bodies().at(1)->surface.faces)
            check(faceColor(*doc.bodies().at(1), face) == red,
                  "A split retains face appearance on both descendants");
        const auto second = doc.bodies().at(1)->surface.faces.rbegin()->first;
        tint(doc, 1, second, blue);
        Id seam = 0;
        const auto adjacency = doc.bodies().at(1)->topology.adjacency(doc.bodies().at(1)->surface);
        for (const auto &[edge, faces] : adjacency.edgeFaces)
            if (faces.size() == 2)
                seam = edge;
        const auto before = doc.bodies().at(1);
        bool rejected = false;
        try {
            doc.eraseEdge(1, seam);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        check(rejected && doc.bodies().at(1) == before,
              "Different colors cannot disappear through face healing");
        tint(doc, 1, second, red);
        doc.eraseEdge(1, seam);
        check(doc.bodies().at(1)->surface.faces.size() == 1 &&
                  faceColor(*doc.bodies().at(1),
                            doc.bodies().at(1)->surface.faces.begin()->first) == red,
              "Matching colors survive face healing");
        Document copied;
        copied.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        tint(copied, 1, 5, red);
        const auto result =
            transformSelected(copied, {{1, TransformKind::Face, 5}},
                              Transform::translation({2, 0, 0}), {}, TransformSpace::World, true);
        const auto copy = result.geometryCopies.at(1).faces.at(5);
        check(faceColor(*copied.bodies().at(1), copy) == red, "Raw copies preserve per-face color");
        Selection selection;
        selection.apply(copied, {{1, SelectionKind::Face, copy}}, SelectionMode::Replace);
        const auto grouped = groupSelected(copied, selection);
        const auto member = grouped.movedGeometry.at(1);
        check(faceColor(*copied.bodies().at(member), copy) == red &&
                  !copied.bodies().at(1)->faceColors.contains(copy),
              "Grouping transfers appearance and prunes retired source face assignments");
        copied.pushPull(member, copy, 2);
        for (const auto &[face, record] : copied.bodies().at(member)->surface.faces)
            check(faceColor(*copied.bodies().at(member), face) == red,
                  "Extruded cap and side walls retain driving appearance");
        const auto beforePaint = *copied.bodies().at(member);
        copied.paint(member, blue);
        check(copied.bodies().at(member)->faceColors.empty() &&
                  copied.bodies().at(member)->color == blue,
              "Painting a context replaces all its own face colors");
        copied.undo();
        check(*copied.bodies().at(member) == beforePaint, "Undo restores per-face appearance");
        auto invalid = std::make_shared<Body>(*copied.bodies().at(member));
        invalid->faceColors[UINT64_MAX] = red;
        rejected = false;
        try {
            copied.apply({"Invalid appearance", {{member, copied.bodies().at(member), invalid}}},
                         copied.revision());
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        check(rejected, "Dangling face colors are rejected");
        std::cout << "Face color inheritance, conflicting merge rejection, copies, grouping, "
                     "extrusion and undo passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
