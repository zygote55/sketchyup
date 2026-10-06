#include "core/components.hpp"
#include "core/copy_array.hpp"
#include "core/groups.hpp"
#include "core/hosted_components.hpp"
#include "geometry/solid.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void volume(const Document &doc, Id host, double expected) {
    const auto &body = *doc.bodies().at(host);
    const auto solid = analyzeSolidShells(body.surface, body.topology);
    check(solid.report.volume && std::abs(*solid.report.volume - expected) < 1e-6,
          "Independent copied host material volume");
}
template <class F> void rejects(Document &doc, F run) {
    const auto bodies = doc.bodies();
    const auto definitions = doc.definitions();
    const auto instances = doc.instances();
    const auto hosted = doc.hostedRecords();
    const auto revision = doc.revision(), next = doc.nextId(), history = doc.historyBytes();
    try {
        run();
    } catch (const std::exception &) {
        check(doc.bodies() == bodies && doc.definitions() == definitions &&
                  doc.instances() == instances && doc.hostedRecords() == hosted &&
                  doc.revision() == revision && doc.nextId() == next &&
                  doc.historyBytes() == history,
              "Failed hosted copy preserves records, allocation, revision and history");
        return;
    }
    throw std::runtime_error("Expected hosted copy rejection");
}
struct Fixture {
    Document doc;
    Id host{}, face{}, root{}, definition{}, member{};
    explicit Fixture(bool cutting = true) {
        host = doc.addFace({{{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}}});
        doc.extrude(host, doc.bodies().at(host)->surface.faces.begin()->first, 1);
        for (const auto &[id, value] : doc.bodies().at(host)->surface.faces)
            if (doc.bodies().at(host)->surface.normal(id).z > .99)
                face = id;
        const auto source = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto sourceFace = doc.bodies().at(source)->surface.faces.begin()->first;
        const auto made = createComponent(doc, source, "Window");
        root = made.instance;
        definition = made.definition;
        member = made.movedGeometry.at(source);
        setComponentGlue(doc, definition,
                         ComponentGlue{member, sourceFace, {1, 1, 0}, {1, 0, 0}, cutting});
        attachComponent(doc, root, host, face, {{3, 3, 1}}, .2);
    }
    TransformResult copy(TransformTargets targets, const Transform &transform, Vec3 pivot = {}) {
        return transformSelected(doc, targets, transform, pivot, TransformSpace::World, true);
    }
    HostOpening opening(Id owner = 0) const {
        return doc.hostedComponents().hosts.at(host)->openings.at(owner ? owner : root);
    }
};
void independentCopies() {
    Fixture f;
    auto &doc = f.doc;
    const auto original = f.opening();
    const auto count = doc.history().total;
    const auto copy = f.copy({{f.root}}, Transform::translation({3, 0, 0})).copies.at(f.root);
    check(doc.hostedComponents().attachments.at(copy)->host == f.host &&
              doc.hostedComponents().attachments.at(copy)->inset == .2 &&
              doc.instances().at(copy)->definition == f.definition &&
              doc.history().total == count + 1,
          "Whole component copy retains host, inset, shared definition and one Undo");
    volume(doc, f.host, 392);
    check(f.opening().vertices == original.vertices && f.opening().jambs == original.jambs,
          "Copy retains original reveal identities");
    const auto copiedOpening = f.opening(copy);
    doc.move(copy, {0, 3, 0});
    check(f.opening(copy).vertices == copiedOpening.vertices &&
              f.opening(copy).jambs == copiedOpening.jambs,
          "Copied attachment moves with stable independent opening identities");
    detachComponent(doc, copy);
    volume(doc, f.host, 396);
    check(doc.hostedComponents().attachments.contains(f.root), "Detach retains source binding");
    rejects(doc, [&] { f.copy({{f.root}}, Transform::translation({0, 0, 1})); });
    rejects(doc, [&] { f.copy({{f.root}}, Transform::translation({.5, 0, 0})); });
    const auto mirrored = f.copy({{f.root}}, Transform::scaling({-1, 1, 1}), {10, 0, 0});
    check(doc.hostedComponents().attachments.contains(mirrored.copies.at(f.root)),
          "Mirrored component copy remains attached");
    volume(doc, f.host, 392);
    setEntityState(doc, f.host, {}, true);
    rejects(doc, [&] { f.copy({{f.root}}, Transform::translation({0, 4, 0})); });
}
void hostAndAssemblies() {
    Fixture f;
    auto &doc = f.doc;
    const auto baked = f.copy({{f.host}}, Transform::translation({30, 0, 0})).copies.at(f.host);
    check(!doc.hostedComponents().hosts.contains(baked),
          "Host-only copy is explicit baked geometry");
    volume(doc, baked, 396);
    detachComponent(doc, f.root);
    volume(doc, f.host, 400);
    volume(doc, baked, 396);
    doc.undo();
    const auto peer = placeComponent(doc, f.definition).instance;
    attachComponent(doc, peer, f.host, f.face, {{7, 3, 1}}, .2);
    rejects(doc, [&] { f.copy({{f.host}, {f.root}}, Transform::translation({30, 0, 0})); });
    const auto reveal = f.opening().jambs.begin()->second;
    auto painted = std::make_shared<Body>(*doc.bodies().at(f.host));
    painted->faceColors[reveal] = {.9f, .2f, .1f};
    doc.apply({"Paint source reveal", {{f.host, doc.bodies().at(f.host), painted}}},
              doc.revision());
    const auto group = createGroup(doc, {f.host, f.root, peer}, "Host assembly");
    doc.transform(group, Transform::rotation({0, 0, 1}, .35) * Transform::scaling({-1.5, .7, 2}));
    const auto original = f.opening();
    const auto result = f.copy({{group}}, Transform::translation({40, 0, 0}));
    const auto host = result.copies.at(f.host), root = result.copies.at(f.root);
    const auto &records = doc.hostedComponents();
    check(records.attachments.at(root)->host == host &&
              records.attachments.at(result.copies.at(peer))->host == host &&
              doc.bodies().at(host)->parent == result.copies.at(group),
          "Reflected affine hierarchy copy remaps the complete host assembly");
    check(records.hosts.at(host)->openings.at(root).jambs == original.jambs &&
              doc.bodies().at(host)->faceColors.at(reveal) == painted->faceColors.at(reveal),
          "Assembly retains copied native reveal IDs and current paint");
    volume(doc, host, 392);
    volume(doc, f.host, 392);
    detachComponent(doc, root);
    volume(doc, host, 396);
    volume(doc, f.host, 392);
    doc.undo();
    doc.undo();
    check(!doc.bodies().contains(host) && doc.hostedComponents().hosts.size() == 1,
          "One Undo removes the complete copied assembly and its bindings");
    doc.redo();
    check(doc.hostedComponents().attachments.at(root)->host == host,
          "Redo restores the same copied relationship identities");
}
void arraysAndAmendment() {
    Fixture f;
    auto &doc = f.doc;
    const auto original = f.opening();
    const auto count = doc.history().total;
    CopyArray array{ArrayMode::Linear, {3, 0, 0}, {0, 0, 1}, 0, 1, false};
    const auto first = copyArraySelected(doc, {{f.root}}, array);
    const auto retired = first.instances[0].copies.at(f.root);
    array.delta = {3.1, 0, 0};
    doc.amendLast(doc.amendmentStamp(),
                  [&](Document &candidate) { copyArraySelected(candidate, {{f.root}}, array); });
    check(!doc.bodies().contains(retired) && doc.hostedComponents().attachments.size() == 2,
          "Fixed-count amendment accepts fresh copied binding identities");
    array.delta = {3, 0, 0};
    array.copies = 3;
    rejects(doc, [&] {
        doc.amendLast(doc.amendmentStamp(), [&](Document &candidate) {
            copyArraySelected(candidate, {{f.root}}, array);
        });
    });
    doc.amendLast(
        doc.amendmentStamp(),
        [&](Document &candidate) { copyArraySelected(candidate, {{f.root}}, array); },
        Document::AmendPolicy::CopyArray);
    check(doc.hostedComponents().attachments.size() == 4 && !doc.bodies().contains(retired) &&
              doc.history().total == count + 1,
          "Array count amendment uses fresh attached copies in one Undo");
    volume(doc, f.host, 384);
    check(f.opening().jambs == original.jambs && f.opening().vertices == original.vertices,
          "Array amendment preserves original opening identities");
    array.copies = 2;
    doc.amendLast(
        doc.amendmentStamp(),
        [&](Document &candidate) { copyArraySelected(candidate, {{f.root}}, array); },
        Document::AmendPolicy::CopyArray);
    check(doc.hostedComponents().attachments.size() == 3, "Shrinking array removes retired cuts");
    volume(doc, f.host, 388);
    doc.undo();
    volume(doc, f.host, 396);
    check(doc.hostedComponents().attachments.size() == 1, "One Undo restores original host only");
    array.copies = 8;
    rejects(doc, [&] { copyArraySelected(doc, {{f.root}}, array); });
    attachComponent(doc, f.root, f.host, f.face, {{13, 10, 1}}, .2);
    CopyArray radial{ArrayMode::Radial, {}, {0, 0, 1}, std::numbers::pi / 2, 3, false};
    copyArraySelected(doc, {{f.root}}, radial, {10, 10, 0});
    volume(doc, f.host, 384);
    check(doc.hostedComponents().attachments.size() == 4,
          "Radial copies retain planar host bindings");
}
void alignmentAndBudget() {
    Fixture f(false);
    const auto host = f.doc.bodies().at(f.host);
    const auto copy = f.copy({{f.root}}, Transform::translation({3, 0, 0})).copies.at(f.root);
    check(f.doc.bodies().at(f.host) == host && f.doc.hostedComponents().attachments.contains(copy),
          "Alignment-only copy preserves geometry and adds real binding metadata");
    f.doc.undo();
    CopyArray tooMany{ArrayMode::Linear, {.01, 0, 0}, {0, 0, 1}, 0, 64, false};
    rejects(f.doc, [&] { copyArraySelected(f.doc, {{f.root}}, tooMany); });
}
void publishDraft(Document &doc, const Document &draft) {
    Edit edit{"Amended copy draft", {}};
    appendSceneMetadataChanges(edit, doc, draft);
    edit.nextIdFloor = draft.nextId();
    for (const auto &[id, body] : draft.bodies()) {
        const auto before = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        if (before != body)
            edit.changes.push_back({id, before, body, {}, {}, {}, true});
    }
    doc.apply(std::move(edit), doc.revision());
}
void amendmentScope() {
    Fixture f(false);
    auto &doc = f.doc;
    const auto foreignHost =
        f.copy({{f.host}}, Transform::translation({0, 0, 0})).copies.at(f.host);
    const auto foreignRoot = placeComponent(doc, f.definition).instance;
    attachComponent(doc, foreignRoot, foreignHost, f.face, {{3, 3, 1}}, .2);
    const auto foreignDefinitionRoot = placeComponent(doc, f.definition).instance;
    makeComponentUnique(doc, foreignDefinitionRoot);
    attachComponent(doc, foreignDefinitionRoot, f.host, f.face, {{3, 3, 1}}, .2);
    f.copy({{f.root}}, Transform::translation({3, 0, 0}));
    const auto stamp = doc.amendmentStamp();
    rejects(doc, [&] {
        doc.amendLast(stamp, [&](Document &candidate) {
            transformSelected(candidate, {{foreignDefinitionRoot}},
                              Transform::translation({4, 0, 0}), {}, TransformSpace::World, true);
        });
    });
    for (int mode : {0, 1, 2})
        rejects(doc, [&] {
            doc.amendLast(
                stamp,
                [&](Document &candidate) {
                    auto draft = candidate.readSnapshot();
                    const auto copied =
                        transformSelected(draft, {{f.root}}, Transform::translation({4, 0, 0}), {},
                                          TransformSpace::World, true)
                            .copies.at(f.root);
                    if (mode == 0)
                        bindComponentAtCurrentPose(draft, copied, foreignHost, f.face, .2);
                    else if (mode == 1)
                        detachComponent(draft, foreignRoot);
                    else
                        detachComponent(draft, copied);
                    publishDraft(candidate, draft);
                },
                Document::AmendPolicy::CopyArray);
        });
    check(doc.canAmend(stamp), "Rejected foreign metadata edits leave the copy amendment eligible");

    Fixture assembly;
    auto &scene = assembly.doc;
    const auto group = createGroup(scene, {assembly.host, assembly.root});
    CopyArray array{ArrayMode::Linear, {30, 0, 0}, {0, 0, 1}, 0, 1, false};
    copyArraySelected(scene, {{group}}, array);
    array.copies = 2;
    scene.amendLast(
        scene.amendmentStamp(),
        [&](Document &candidate) { copyArraySelected(candidate, {{group}}, array); },
        Document::AmendPolicy::CopyArray);
    check(scene.hostedComponents().hosts.size() == 3 &&
              scene.hostedComponents().attachments.size() == 3,
          "Assembly array count amendment remaps fresh host and attachment identities");
    for (const auto &[host, record] : scene.hostedComponents().hosts)
        volume(scene, host, 396);
    scene.undo();
    check(scene.hostedComponents().hosts.size() == 1 &&
              scene.hostedComponents().attachments.size() == 1,
          "Assembly array amendment retains one complete Undo");
}
} // namespace
int main() {
    try {
        independentCopies();
        hostAndAssemblies();
        arraysAndAmendment();
        alignmentAndBudget();
        amendmentScope();
        std::cout << "Hosted copies, assembly remapping, arrays, amendment, locks and atomic "
                     "bounds passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
