#include "core/component_glue.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/hosted_components.hpp"
#include "geometry/solid.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-6, message); }
template <class F> void rejects(Document &doc, F operation) {
    const auto stamp = doc.saveStamp();
    const auto revision = doc.revision(), history = doc.historyBytes();
    try {
        operation();
    } catch (const std::exception &) {
        check(doc.isCurrentSnapshot(stamp) && doc.revision() == revision &&
                  doc.historyBytes() == history,
              "Rejected hosted edit must leave document and history exact");
        return;
    }
    throw std::runtime_error("Expected hosted edit rejection");
}
Id wall(Document &doc, double x = 0) {
    auto id = doc.addFace({{{x, 0, 0}, {x + 10, 0, 0}, {x + 10, 10, 0}, {x, 10, 0}}});
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, 1);
    return id;
}
Id top(const Body &body) {
    for (const auto &[id, face] : body.surface.faces)
        if (body.surface.normal(id).z > .99)
            return id;
    throw std::runtime_error("Missing wall top");
}
void volume(const Document &doc, Id id, double expected) {
    const auto &body = *doc.bodies().at(id);
    const auto solid = analyzeSolidShells(body.surface, body.topology);
    check(solid.report.status == "validated_shells" && solid.report.volume &&
              std::abs(*solid.report.volume - expected) < 1e-6,
          "Host has independent expected closed material volume");
}
struct Fixture {
    Document doc;
    Id host{}, face{}, root{}, member{}, definition{};
    ComponentGlue glue;
    Fixture(bool cutting = true) {
        host = wall(doc);
        face = top(*doc.bodies().at(host));
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto reference = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto component = createComponent(doc, body, "Window");
        root = component.instance;
        definition = component.definition;
        member = component.movedGeometry.at(body);
        glue = {member, reference, {1, 1, 0}, {1, 0, 0}, cutting};
        setComponentGlue(doc, definition, glue);
    }
    void attach(Vec3 anchor = {3, 3, 1}, double inset = 0) {
        attachComponent(doc, root, host, face, {anchor}, inset);
    }
    const HostOpening &opening(Id owner = 0) const {
        return doc.hostedComponents().hosts.at(host)->openings.at(owner ? owner : root);
    }
};
void lifecycle() {
    Fixture f;
    auto &doc = f.doc;
    const auto baseline = doc.readSnapshot();
    const auto count = doc.history().total;
    auto prepared = doc.prepareEdit(
        [&](Document &draft) { attachComponent(draft, f.root, f.host, f.face, {{3, 3, 1}}, .2); });
    check(doc.hostedComponents().attachments.empty(), "Preview keeps original scene unbound");
    volume(prepared.snapshot(), f.host, 96);
    doc.applyPrepared(prepared);
    rejects(doc, [&] { doc.applyPrepared(prepared); });
    check(doc.history().total == count + 1 && doc.hostedComponents().attachments.size() == 1,
          "Placement, binding and cut publish together in one Undo");
    near(doc.worldTransform(f.root).point({1, 1, 0}), {3, 3, 1.2}, "Explicit inset is retained");
    check(baseline.hostedComponents().hosts.empty() &&
              doc.readSnapshotBytes() > baseline.readSnapshotBytes(),
          "Immutable snapshot shares old records and new baseline has an admission charge");
    const auto first = f.opening();
    const auto stamp = doc.amendmentStamp();
    doc.amendLast(stamp, [&](Document &draft) {
        attachComponent(draft, f.root, f.host, f.face, {{4, 3, 1}}, .2);
    });
    near(doc.worldTransform(f.root).point({1, 1, 0}), {4, 3, 1.2},
         "Numeric amendment replaces attachment");
    check(doc.history().total == count + 1, "Amend retains one history entry");
    doc.undo();
    volume(doc, f.host, 100);
    check(doc.hostedComponents().attachments.empty(), "Undo clears cut and binding");
    doc.redo();
    volume(doc, f.host, 96);
    const auto old = f.opening();
    const auto peer = placeComponent(doc, f.definition).instance;
    attachComponent(doc, peer, f.host, f.face, {{7, 7, 1}});
    volume(doc, f.host, 92);
    const auto neighbor = f.opening(peer);
    const auto reveal = old.jambs.begin()->second;
    auto painted = std::make_shared<Body>(*doc.bodies().at(f.host));
    painted->faceColors[reveal] = {.9f, .2f, .1f};
    doc.apply({"Paint reveal", {{f.host, doc.bodies().at(f.host), painted}}}, doc.revision());
    doc.move(f.root, {1, 0, 0});
    near(doc.worldTransform(f.root).point({1, 1, 0}), {5, 3, 1.2},
         "Attached component moves along host");
    check(f.opening().vertices == old.vertices && f.opening().jambs == old.jambs &&
              f.opening(peer) == neighbor &&
              doc.bodies().at(f.host)->faceColors.at(reveal) == painted->faceColors.at(reveal),
          "Moving preserves reveal identity, paint and neighboring opening");
    rejects(doc, [&] { doc.move(f.root, {0, 0, 1}); });
    rejects(doc, [&] { doc.move(f.root, {30, 0, 0}); });
    rejects(doc, [&] { doc.move(f.root, {2, 4, 0}); });
    rejects(doc, [&] { doc.pushPull(f.host, f.face, 1); });
    detachComponent(doc, f.root);
    volume(doc, f.host, 96);
    check(f.opening(peer) == neighbor, "Detach restores its region and retains neighbor");
    doc.undo();
    volume(doc, f.host, 92);
    Selection selection;
    selection.sync(doc);
    selection.apply(doc, {{peer, SelectionKind::Body, 0}}, SelectionMode::Replace);
    eraseSelected(doc, selection);
    volume(doc, f.host, 96);
    check(!doc.hostedComponents().attachments.contains(peer),
          "Delete component restores its opening");
    doc.undo();
    volume(doc, f.host, 92);
    const auto pose = doc.worldTransform(f.root);
    doc.erase(f.host);
    check(doc.hostedComponents().hosts.empty() && doc.hostedComponents().attachments.empty() &&
              doc.worldTransform(f.root) == pose,
          "Deleting host releases surviving placements without moving them");
    doc.undo();
    volume(doc, f.host, 92);
    validateHostedComponents(doc.hostedComponents(), doc.bodies(), doc.definitions(),
                             doc.instances());
    (void)first;
}
void sharedEditsAndRehosting() {
    Fixture f;
    auto &doc = f.doc;
    f.attach();
    const auto peer = placeComponent(doc, f.definition).instance;
    attachComponent(doc, peer, f.host, f.face, {{7, 7, 1}});
    const auto original = f.opening();
    editComponentDefinition(doc, f.definition, [&](Document &draft) {
        draft.transform(f.member, Transform::scaling({1.5, 1, 1}), f.root);
        return ChangeReport{};
    });
    volume(doc, f.host, 88);
    const auto source = resolveComponentGlue(*doc.definitions().at(f.definition));
    near(doc.worldTransform(f.root).point(source.frame.origin), {3, 3, 1},
         "Shared resize keeps first glue anchor");
    near(doc.worldTransform(peer).point(source.frame.origin), {7, 7, 1},
         "Shared resize keeps second glue anchor");
    check(f.opening().vertices == original.vertices && f.opening().jambs == original.jambs,
          "Shared resize retains native opening identities");
    makeComponentUnique(doc, peer);
    auto alignmentOnly = f.glue;
    alignmentOnly.cutsOpening = false;
    setComponentGlue(doc, doc.instances().at(peer)->definition, alignmentOnly);
    volume(doc, f.host, 94);
    check(doc.hostedComponents().attachments.size() == 2 &&
              !doc.hostedComponents().hosts.at(f.host)->openings.contains(peer),
          "Alignment-only unique peer stays attached but no longer cuts");
    const auto next = wall(doc, 20), nextFace = top(*doc.bodies().at(next));
    attachComponent(doc, f.root, next, nextFace, {{24, 4, 1}});
    volume(doc, f.host, 100);
    volume(doc, next, 94);
    check(doc.hostedComponents().hosts.size() == 2,
          "Rehost restores old wall and retains alignment-only baseline");
    doc.undo();
    volume(doc, f.host, 94);
    volume(doc, next, 100);
    setComponentGlue(doc, f.definition, {});
    volume(doc, f.host, 100);
    check(!doc.hostedComponents().attachments.contains(f.root) &&
              doc.hostedComponents().attachments.contains(peer),
          "Removing glue automatically detaches only its placements");
    doc.undo();
    volume(doc, f.host, 94);
    bakeHostedComponents(doc, f.host);
    volume(doc, f.host, 94);
    check(doc.hostedComponents().hosts.empty(), "Bake keeps geometry and releases all attachments");
    doc.pushPull(f.host, f.face, .2);
    doc.undo();
    doc.undo();
    volume(doc, f.host, 94);
    check(doc.hostedComponents().attachments.size() == 2,
          "Undo bake restores complete attachment state");
}
void transformedParentsAndLocks() {
    Fixture f;
    auto &doc = f.doc;
    const auto parent = createGroup(doc, {f.host, f.root});
    Transform shear;
    shear.m[8] = .4;
    doc.transform(parent,
                  Transform::translation({20, 30, 40}) * shear * Transform::scaling({-2, 3, .5}));
    f.attach({4, 4, 1});
    const auto records = doc.hostedRecords();
    const auto opening = f.opening();
    const auto pose = doc.worldTransform(f.root);
    doc.move(parent, {1, 2, 3});
    near(doc.worldTransform(f.root).point({}), pose.point({}) + Vec3{1, 2, 3},
         "Common parent motion preserves relative attachment");
    check(doc.hostedComponents() == *records && f.opening() == opening,
          "Common ancestor motion avoids baseline and cut coordinate drift");
    const auto old = doc.worldTransform(f.root);
    doc.move(f.host, {1, 0, 0});
    near(doc.worldTransform(f.root).point({}), old.point({}) + Vec3{1, 0, 0},
         "Host motion carries attached placement");
    setEntityState(doc, f.host, {}, true);
    rejects(doc, [&] { detachComponent(doc, f.root); });
    rejects(doc, [&] { bakeHostedComponents(doc, f.host); });
    rejects(doc, [&] { doc.move(f.root, {1, 0, 0}); });
    setEntityState(doc, f.host, {}, false);
    setEntityState(doc, f.root, {}, true);
    rejects(doc, [&] { detachComponent(doc, f.root); });
    rejects(doc, [&] { doc.move(f.host, {1, 0, 0}); });
    setEntityState(doc, f.root, {}, false);
    validateHostedComponents(doc.hostedComponents(), doc.bodies(), doc.definitions(),
                             doc.instances());
}
void alignmentMetadataAndComposedEdits() {
    Fixture f(false);
    auto &doc = f.doc;
    doc.transform(f.root, Transform::translation({2, 2, 1}));
    const auto bodies = doc.bodies();
    const auto before = doc.readSnapshot();
    bindComponentAtCurrentPose(doc, f.root, f.host, f.face, 0);
    check(doc.bodies() == bodies && doc.hostedComponents().attachments.size() == 1,
          "Binding current aligned pose can be metadata-only");
    const auto after = doc.readSnapshot();
    doc.undo();
    Edit composed{"Composed metadata binding", {}};
    appendSceneMetadataChanges(composed, doc, after);
    doc.apply(composed, doc.revision());
    check(doc.hostedComponents() == after.hostedComponents(),
          "Composed metadata publishes exact validated binding");
    auto snapshot = doc.readSnapshot();
    auto forged = std::make_shared<HostedComponents>(doc.hostedComponents());
    auto attachment = std::make_shared<ComponentAttachment>(*forged->attachments.at(f.root));
    attachment->frame = Transform::translation({0, 0, 2}) * attachment->frame;
    forged->attachments[f.root] = attachment;
    Edit invalid{"Invalid resolved binding", {}};
    invalid.hosted = HostedChange{doc.hostedRecords(), forged};
    invalid.hostedResolved = true;
    rejects(doc, [&] { doc.apply(invalid, doc.revision()); });
    setEntityState(doc, f.host, {}, true);
    rejects(doc, [&] { detachComponent(doc, f.root); });
    setEntityState(doc, f.host, {}, false);
    const auto unrelated =
        placeComponent(doc, f.definition, Transform::translation({6, 6, 1})).instance;
    doc.move(f.root, {1, 0, 0});
    const auto amend = doc.amendmentStamp();
    doc.amendLast(amend, [&](Document &draft) { draft.move(f.root, {2, 0, 0}); });
    near(doc.worldTransform(f.root).point({1, 1, 0}), {5, 3, 1},
         "Metadata-only host movement can be amended");
    const auto scoped = doc.amendmentStamp();
    rejects(doc, [&] {
        doc.amendLast(scoped, [&](Document &draft) {
            bindComponentAtCurrentPose(draft, unrelated, f.host, f.face, 0);
        });
    });
    check(snapshot.hostedComponents() == after.hostedComponents(),
          "Retained snapshot remains immutable");
    // A caller-owned immutable pointer may still have a mutable alias: freeze at publication.
    auto candidate = std::make_shared<HostedComponents>(doc.hostedComponents());
    auto alias = std::make_shared<ComponentAttachment>(*candidate->attachments.at(f.root));
    candidate->attachments[f.root] = alias;
    Edit freeze{"Freeze attachment aliases", {}};
    freeze.hosted = HostedChange{doc.hostedRecords(), candidate};
    freeze.hostedResolved = true;
    doc.apply(freeze, doc.revision());
    alias->face = 999;
    candidate->hosts.clear();
    check(doc.hostedComponents().attachments.at(f.root)->face == f.face &&
              doc.hostedComponents().hosts.size() == 1,
          "Published records freeze nested aliases");
    (void)before;
}
void replacementsAxesAndScope() {
    Fixture f;
    auto &doc = f.doc;
    f.attach();
    const auto anchor = doc.worldTransform(f.root).point({1, 1, 0});
    const auto opening = f.opening();
    setComponentAxes(doc, f.definition,
                     Transform::translation({3, 4, 5}) * Transform::scaling({-2, 3, .5}));
    const auto resolved = resolveComponentGlue(*doc.definitions().at(f.definition));
    near(doc.worldTransform(f.root).point(resolved.frame.origin), anchor,
         "Changing definition axes retains world glue anchor");
    volume(doc, f.host, 96);
    check(f.opening().vertices == opening.vertices && f.opening().jambs == opening.jambs,
          "Axes change keeps opening identities");
    const auto sourceFace = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto face = doc.bodies().at(sourceFace)->surface.faces.begin()->first;
    const auto replacement = createComponent(doc, sourceFace, "Replacement");
    const auto member = replacement.movedGeometry.at(sourceFace);
    setComponentGlue(doc, replacement.definition,
                     ComponentGlue{member, face, {.5, .5, 0}, {1, 0, 0}, true});
    // Replacement uses the stored glue frame, including the signed/nonuniform
    // affine scale introduced by the explicitly changed component axes.
    replaceComponent(doc, f.root, replacement.definition);
    check(doc.hostedComponents().attachments.contains(f.root),
          "Gluing replacement retains attachment");
    validateHostedComponents(doc.hostedComponents(), doc.bodies(), doc.definitions(),
                             doc.instances());
    const auto replacementFrame = doc.hostedComponents().attachments.at(f.root)->frame;
    near(doc.worldTransform(f.root).point({.5, .5, 0}),
         doc.worldTransform(f.host).point(replacementFrame.point({})),
         "Replacement aligns its explicit source anchor to the saved frame");
    setComponentGlue(doc, replacement.definition, {});
    volume(doc, f.host, 100);
    check(doc.hostedComponents().attachments.empty(),
          "Non-gluing replacement behavior releases attachment");
    doc.undo();
    doc.undo();
    volume(doc, f.host, 96);
    rejects(doc, [&] {
        editComponentDefinition(doc, f.definition, [&](Document &draft) {
            return draft.eraseFace(f.member, f.glue.face);
        });
    });
    rejects(doc, [&] { createComponent(doc, f.host, "Invalid component host"); });
    const auto unboundFace = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto unbound = createComponent(doc, unboundFace, "Non-gluing replacement");
    replaceComponent(doc, f.root, unbound.definition);
    volume(doc, f.host, 100);
    check(doc.hostedComponents().attachments.empty(),
          "Replacing with non-gluing definition detaches");
    doc.undo();
    volume(doc, f.host, 96);
}
} // namespace
int main() {
    try {
        lifecycle();
        sharedEditsAndRehosting();
        transformedParentsAndLocks();
        alignmentMetadataAndComposedEdits();
        replacementsAxesAndScope();
        std::cout << "Hosted placement, cuts, motion, shared resizing, rehost, deletion, locks, "
                     "previews, amendment and Undo passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
