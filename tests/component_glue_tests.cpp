#include "core/component_glue.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-8, message); }
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected glue behavior rejection");
}
struct Fixture {
    Document doc;
    Id definition{}, root{}, member{}, face{};
    ComponentGlue glue;
    Fixture() {
        const auto body = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                       {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        face = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto created = createComponent(doc, body, "Window frame");
        definition = created.definition;
        root = created.instance;
        member = created.movedGeometry.at(body);
        glue = {member, face, {2, 2, 0}, {1, 0, 0}, true};
    }
};
void behaviorAndHistory() {
    Fixture f;
    auto &doc = f.doc;
    const auto peer = placeComponent(doc, f.definition, Transform::translation({10, 0, 0}));
    const auto scene = doc.bodies();
    const auto original = doc.definitions().at(f.definition);
    const auto history = doc.history().total;
    const auto snapshot = doc.readSnapshot();
    const auto prepared =
        doc.prepareEdit([&](Document &draft) { setComponentGlue(draft, f.definition, f.glue); });
    check(doc.definitions().at(f.definition) == original &&
              prepared.snapshot().definitions().at(f.definition)->glue == f.glue,
          "Glue configuration previews privately");
    doc.applyPrepared(prepared);
    check(doc.bodies() == scene && doc.history().total == history + 1 &&
              !snapshot.definitions().at(f.definition)->glue,
          "Definition-only behavior keeps scene and retained snapshot unchanged");
    const auto resolved = resolveComponentGlue(*doc.definitions().at(f.definition));
    near(resolved.frame.origin, {2, 2, 0}, "A ring's glue anchor can lie inside its hole");
    near(resolved.frame.normal, {0, 0, 1}, "Canonical face physical normal");
    check(resolved.profile == std::vector<Vec3>{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
          "Cut profile references outer boundary, not the ring's material silhouette");
    doc.undo();
    check(!doc.definitions().at(f.definition)->glue, "Undo restores absent glue behavior");
    doc.redo();
    check(doc.definitions().at(f.definition)->glue == f.glue, "Redo restores exact behavior");
    const auto revision = doc.revision();
    setComponentGlue(doc, f.definition, f.glue);
    check(doc.revision() == revision, "Unchanged glue behavior is a no-op");
    editComponentDefinition(doc, f.definition, [&](Document &draft) {
        draft.transform(f.member, Transform::scaling({2, 1, 1}), f.root);
        return ChangeReport{};
    });
    const auto resized = resolveComponentGlue(*doc.definitions().at(f.definition));
    near(resized.frame.origin, {4, 2, 0},
         "Shared member resize retains a member-local glue anchor");
    near(resized.profile[1], {8, 0, 0}, "Shared geometry edits update the referenced outline");
    check(doc.bodies().at(peer.instance)->transform == Transform::translation({10, 0, 0}),
          "Shared changes preserve instance-specific placement");
    const auto before = doc.saveStamp();
    rejects([&] {
        editComponentDefinition(doc, f.definition,
                                [&](Document &draft) { return draft.eraseFace(f.member, f.face); });
    });
    check(doc.isCurrentSnapshot(before), "Deleting the referenced face rejects atomically");
    const auto beforeAxes = doc.worldTransform(f.root).point(resized.frame.origin);
    const auto profilePoint = doc.worldTransform(f.root).point(resized.profile[1]);
    setComponentAxes(doc, f.definition,
                     Transform::translation({3, 5, 7}) * Transform::scaling({-2, 3, .5}));
    const auto reoriented = resolveComponentGlue(*doc.definitions().at(f.definition));
    near(doc.worldTransform(f.root).point(reoriented.frame.origin), beforeAxes,
         "Changing component axes preserves world glue origin");
    near(doc.worldTransform(f.root).point(reoriented.profile[1]), profilePoint,
         "Changing component axes preserves world cut outline");
    const auto unique = makeComponentUnique(doc, peer.instance);
    check(doc.definitions().at(unique.definition)->glue == f.glue,
          "Make Unique retains its own glue reference");
    auto noncutting = f.glue;
    noncutting.cutsOpening = false;
    setComponentGlue(doc, unique.definition, noncutting);
    check(resolveComponentGlue(*doc.definitions().at(unique.definition)).profile.empty() &&
              doc.definitions().at(f.definition)->glue->cutsOpening,
          "Unique alignment-only behavior does not change shared peers");
    setEntityState(doc, f.root, {}, true);
    const auto locked = doc.saveStamp();
    rejects([&] { setComponentGlue(doc, f.definition, {}); });
    check(doc.isCurrentSnapshot(locked), "Locked shared placement protects glue behavior");
    setEntityState(doc, f.root, {}, false);
    setComponentGlue(doc, f.definition, {});
    check(!doc.definitions().at(f.definition)->glue, "Behavior can be explicitly removed");
}
void nestedFramesAndFailures() {
    Fixture f;
    auto prototype = std::make_shared<ComponentDefinition>(*f.doc.definitions().at(f.definition));
    prototype->glue = f.glue;
    auto geometry = std::make_shared<Body>(*prototype->members.at(f.member));
    Transform shear;
    shear.m[2] = .2;
    shear.m[4] = .7;
    shear.m[8] = .3;
    shear.m[9] = -.4;
    geometry->transform = shear * Transform::scaling({-2, 3, .5});
    prototype->members[f.member] = geometry;
    const auto result = resolveComponentGlue(*prototype);
    near(result.frame.origin, geometry->transform.point({2, 2, 0}), "Transformed member anchor");
    near(result.frame.normal, normalized({-.2, .14, 1}), "Reflected/sheared physical face normal");
    near(result.frame.xAxis, normalized({-1, 0, -.2}), "Explicit transformed in-plane tangent");
    near(result.profile[1], geometry->transform.point({4, 0, 0}), "Native member cut coordinates");
    for (int variant = 0; variant < 7; ++variant) {
        auto bad = f.glue;
        if (variant == 0)
            bad.member = 999;
        if (variant == 1)
            bad.face = 999;
        if (variant == 2)
            bad.anchor.z = 1;
        if (variant == 3)
            bad.tangent = {0, 0, 1};
        if (variant == 4)
            bad.tangent.x = std::numeric_limits<double>::quiet_NaN();
        if (variant == 5)
            bad.member = f.root;
        if (variant == 6)
            bad.anchor.x = std::numeric_limits<double>::infinity();
        const auto before = f.doc.saveStamp();
        rejects([&] { setComponentGlue(f.doc, f.definition, bad); });
        check(f.doc.isCurrentSnapshot(before), "Invalid references and frames reject atomically");
    }
    prototype->references[f.member] = 99;
    rejects([&] { resolveComponentGlue(*prototype); });
    prototype->references.clear();
    geometry->parent = f.member;
    rejects([&] { resolveComponentGlue(*prototype); });
}
} // namespace
int main() {
    try {
        behaviorAndHistory();
        nestedFramesAndFailures();
        std::cout << "Canonical glue references, shared geometry, unique behavior, axes, locks "
                     "and immutable previews passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
