// R082.ff: incremental edit validation must accept, reject, report and store exactly
// what full-document validation does. Every apply below runs with the full-validation
// oracle on, so each step is also validated by recounting the whole candidate.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <random>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
DocumentTotals recount(const Document &doc) {
    DocumentTotals totals;
    for (const auto &[id, body] : doc.bodies()) {
        totals.records += 1;
        totals.vertices += body->surface.vertices.size();
        totals.faces += body->surface.faces.size();
        totals.wires += body->surface.wires.size();
        totals.edges += body->topology.edges.size();
        totals.curves += body->curves.size();
        totals.guides += body->guides.size();
    }
    return totals;
}
void checkTotals(const Document &doc, const std::string &context) {
    check(doc.materializedTotals() == recount(doc), context + ": running totals drifted");
    doc.verifyIncrementalState();
}
Id prism(Document &doc, int sides, double radius = 1, Vec3 offset = {}) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back(offset + Vec3{radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    return body;
}
template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const ValidationOracleMismatch &) {
        throw;
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
// A committed batch: private staging, then one composed apply, as the automation
// batch driver does. A failure anywhere leaves the document untouched.
void commitBatch(Document &doc, const std::function<void(Document &)> &operations) {
    auto staged = doc.readSnapshot();
    operations(staged);
    Edit edit{"Batch", {}};
    appendSceneMetadataChanges(edit, doc, staged);
    std::set<Id> ids;
    for (const auto &[id, body] : doc.bodies())
        ids.insert(id);
    for (const auto &[id, body] : staged.bodies())
        ids.insert(id);
    for (auto id : ids) {
        const auto before = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        const auto after = staged.bodies().contains(id) ? staged.bodies().at(id) : nullptr;
        if (before != after)
            edit.changes.push_back({id, before, after, {}, {}, {}, true});
    }
    edit.nextIdFloor = staged.nextId();
    doc.apply(std::move(edit), doc.revision());
}
struct Fingerprint {
    std::uint64_t revision;
    DocumentTotals totals;
    size_t bodies, instances, definitions, bytes;
    bool operator==(const Fingerprint &) const = default;
};
Fingerprint fingerprint(const Document &doc) {
    return {doc.revision(),         doc.materializedTotals(), doc.bodies().size(),
            doc.instances().size(), doc.definitions().size(), doc.readSnapshotBytes()};
}

void randomizedEquivalence(std::uint32_t seed, int steps) {
    std::mt19937 random(seed);
    auto pick = [&](size_t size) {
        return size_t(std::uniform_int_distribution<size_t>(0, size - 1)(random));
    };
    auto chance = [&](double p) {
        return std::uniform_real_distribution<double>(0, 1)(random) < p;
    };
    Document doc;
    // Small, large (limit pressure) and nested definitions.
    const auto small = createComponent(doc, prism(doc, 6), "Small").definition;
    const auto large = createComponent(doc, prism(doc, 200, 2, {10, 0, 0}), "Large").definition;
    // Unchanged loose geometry close to the vertex limit keeps sequences near the
    // boundary without re-validating large records on every step.
    for (int i = 0; i < 85; ++i) {
        std::vector<Vec3> loop;
        for (int j = 0; j < 1000; ++j) {
            const auto angle = 2 * std::numbers::pi * j / 1000;
            loop.push_back({50 + std::cos(angle), std::sin(angle), double(i)});
        }
        doc.addFace({loop}, "Ballast");
    }
    const auto inner = placeComponent(doc, small, Transform::translation({0, 5, 0})).instance;
    const auto loose = prism(doc, 4, 1, {3, 5, 0});
    const auto outer = createGroup(doc, {inner, loose}, "Outer");
    createComponent(doc, outer, "Nested");
    checkTotals(doc, "setup");
    size_t accepted = 0, rejected = 0, limitRejected = 0, batchFailures = 0;
    auto bodies = [&] {
        std::vector<Id> ids;
        for (const auto &[id, body] : doc.bodies())
            ids.push_back(id);
        return ids;
    };
    auto anyBody = [&]() -> Id {
        const auto ids = bodies();
        return ids.empty() ? 0 : ids[pick(ids.size())];
    };
    auto anyInstance = [&]() -> Id {
        if (doc.instances().empty())
            return 0;
        auto it = doc.instances().begin();
        std::advance(it, pick(doc.instances().size()));
        return it->first;
    };
    auto anyGroup = [&]() -> Id {
        std::vector<Id> groups;
        for (const auto &[id, body] : doc.bodies())
            if (body->kind == BodyKind::Group)
                groups.push_back(id);
        return groups.empty() ? 0 : groups[pick(groups.size())];
    };
    auto translation = [&] {
        return Transform::translation(
            {double(int(pick(80))) - 40, double(int(pick(80))) - 40, double(int(pick(5)))});
    };
    auto liveDefinition = [&](const Document &target) {
        std::vector<Id> live;
        for (const auto &[id, record] : target.definitions())
            live.push_back(id);
        return live[pick(live.size())];
    };
    auto place = [&](Document &target) {
        // Bias towards the large definition so sequences reach the vertex limit.
        const auto definition =
            chance(.5) && target.definitions().contains(large) ? large : liveDefinition(target);
        const Id parent = chance(.25) ? anyGroup() : 0;
        placeComponent(target, definition, translation(), parent);
    };
    for (int step = 0; step < steps; ++step) {
        // Placement-heavy weights so sequences grow to the limits between rollbacks.
        static const std::discrete_distribution<int> weights{
            {4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1}};
        auto distribution = weights;
        const auto operation = size_t(distribution(random));
        const auto before = fingerprint(doc);
        const auto message = rejection([&] {
            switch (operation) {
            case 0:
            case 1:
            case 2:
                place(doc);
                break;
            case 3:
                if (const auto root = anyInstance())
                    makeComponentUnique(doc, root);
                break;
            case 4:
                if (const auto body = anyBody())
                    doc.erase(body);
                break;
            case 5:
                for (auto i = pick(3); i-- > 0;)
                    doc.undo();
                break;
            case 6:
                for (auto i = pick(3); i-- > 0;)
                    doc.redo();
                break;
            case 7: {
                // Loose geometry of random size reaches the limit through apply itself.
                std::vector<Vec3> loop;
                const auto sides = 3 + pick(300);
                for (size_t i = 0; i < sides; ++i) {
                    const auto angle = 2 * std::numbers::pi * double(i) / double(sides);
                    loop.push_back({std::cos(angle), std::sin(angle), double(step)});
                }
                doc.addFace({loop});
            } break;
            case 8:
                if (const auto body = anyBody())
                    doc.move(body, {1, 0, 0});
                break;
            case 9:
                if (const auto body = anyBody())
                    doc.paint(body, {float(pick(10)) / 10, .5f, .25f});
                break;
            case 10:
                if (const auto body = anyBody())
                    setEntityState(doc, body, {}, chance(.5));
                break;
            case 11: {
                const bool fail = chance(.3);
                const auto count = 1 + pick(4);
                const auto message = rejection([&] {
                    commitBatch(doc, [&](Document &staged) {
                        for (size_t i = 0; i < count; ++i) {
                            if (chance(.6))
                                place(staged);
                            else
                                staged.addFace(
                                    {{{5, 5, double(i)}, {6, 5, double(i)}, {5, 6, double(i)}}});
                            if (fail && i + 1 == count)
                                throw std::runtime_error("Injected batch failure");
                        }
                    });
                });
                if (!message.empty()) {
                    ++batchFailures;
                    throw std::runtime_error(message);
                }
                break;
            }
            case 12: {
                const auto stamp = doc.amendmentStamp();
                if (doc.canAmend(stamp))
                    doc.amendLast(stamp, [&](Document &candidate) {
                        placeComponent(candidate, liveDefinition(candidate), translation());
                    });
                break;
            }
            case 13: {
                const auto page = doc.history(0, 1);
                const auto delta = std::min<size_t>(page.position, pick(4));
                doc.navigateHistory(chance(.5) ? page.position - delta
                                               : std::min(page.total, page.position + pick(4)),
                                    doc.revision());
                break;
            }
            case 14:
                if (const auto root = anyInstance())
                    replaceComponent(doc, root, liveDefinition(doc));
                break;
            }
        });
        const auto context = "seed " + std::to_string(seed) + " step " + std::to_string(step) +
                             " operation " + std::to_string(operation);
        checkTotals(doc, context);
        if (message.empty())
            ++accepted;
        else {
            ++rejected;
            if (std::getenv("SKETCHYUP_TRACE_REJECTIONS"))
                std::cerr << context << ": " << message << " (vertices "
                          << doc.materializedTotals().vertices << ")\n";
            if (message.find("limits") != std::string::npos)
                ++limitRejected;
            // Rejected edits, including failed batches, publish nothing.
            check(fingerprint(doc) == before,
                  context + ": rejected step changed the document (" + message + ")");
        }
    }
    std::cout << "seed " << seed << ": " << accepted << " accepted, " << rejected << " rejected ("
              << limitRejected << " at limits, " << batchFailures << " failed batches)\n";
    check(accepted > steps / 4 && rejected > 0 && limitRejected > 0 && batchFailures > 0,
          "Randomized sequence did not cover acceptance, rejection, limits and failed batches");
}

void placementBoundary() {
    // One 1000-sided prism instance materializes exactly 2000 vertices, so fifty
    // instances meet the 100000-vertex limit exactly; every other total stays below.
    static_assert(DocumentLimits::vertices == 100000);
    Document doc;
    const auto definition = createComponent(doc, prism(doc, 1000), "Boundary").definition;
    check(doc.materializedTotals().vertices == 2000, "Unexpected boundary fixture size");
    for (int i = 1; i < 50; ++i)
        placeComponent(doc, definition, Transform::translation({3.0 * i, 0, 0}));
    check(doc.materializedTotals().vertices == DocumentLimits::vertices,
          "Placements up to the limit must succeed");
    checkTotals(doc, "at limit");
    const auto full = fingerprint(doc);
    check(rejection([&] { placeComponent(doc, definition); }) ==
              "Component placement exceeds document editing limits",
          "One placement past the limit must be rejected with the existing message");
    check(rejection([&] { doc.addFace({{{0, 0, 9}, {1, 0, 9}, {0, 1, 9}}}); }) ==
              "Document complexity exceeds editing limits",
          "Any record past the limit must be rejected by apply");
    check(fingerprint(doc) == full, "Rejected boundary edits must not publish");
    doc.undo();
    checkTotals(doc, "after undo");
    placeComponent(doc, definition, Transform::translation({0, 9, 0}));
    check(doc.materializedTotals().vertices == DocumentLimits::vertices,
          "A placement freed by undo must fit again");
    doc.undo();
    check(doc.materializedTotals().vertices == DocumentLimits::vertices - 2000,
          "Undo releases one placement");
    doc.redo();
    check(doc.materializedTotals().vertices == DocumentLimits::vertices,
          "Redo restores the limit total");
    checkTotals(doc, "after redo");
}

void bodyBoundary() {
    // A one-triangle definition materializes two records per instance (frame and
    // geometry). Fill the remaining body allowance with empty groups in one edit.
    static_assert(DocumentLimits::bodies == 10000);
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
    const auto definition = createComponent(doc, body, "Triangle").definition;
    const auto existing = doc.bodies().size();
    const size_t placements = 6;
    Edit fill{"Fill", {}};
    for (size_t i = 0; i < DocumentLimits::bodies - existing - 2 * placements; ++i) {
        auto group = std::make_shared<Body>();
        group->id = doc.nextId() + i;
        group->kind = BodyKind::Group;
        group->name = "Empty";
        fill.changes.push_back({group->id, nullptr, group});
    }
    doc.apply(std::move(fill), doc.revision());
    for (size_t i = 0; i < placements; ++i)
        placeComponent(doc, definition, Transform::translation({2.0 * i, 0, 0}));
    check(doc.bodies().size() == DocumentLimits::bodies, "Placements up to the body limit succeed");
    checkTotals(doc, "body limit");
    const auto full = fingerprint(doc);
    check(rejection([&] { placeComponent(doc, definition); }) ==
              "Component placement exceeds document editing limits",
          "One placement past the body limit must be rejected with the existing message");
    check(fingerprint(doc) == full, "Rejected body-limit placement must not publish");
}

void restoreAndSnapshots() {
    Document doc;
    const auto definition = createComponent(doc, prism(doc, 8), "Restore").definition;
    for (int i = 0; i < 5; ++i)
        placeComponent(doc, definition, Transform::translation({3.0 * i, 3, 0}));
    const auto locked = placeComponent(doc, definition, Transform::translation({0, 9, 0})).instance;
    setEntityState(doc, locked, {}, true);
    // An erased record leaves a detached allocator floor while history retains it.
    doc.erase(doc.addFace({{{0, 0, 4}, {1, 0, 4}, {0, 1, 4}}}));
    checkTotals(doc, "before restore");
    // Restore recounts from scratch; a read snapshot carries the same totals.
    Document restored;
    restored.restore(doc.identity(), doc.nextId(), doc.bodies(), doc.revision(), doc.definitions(),
                     doc.instances(), doc.nextDefinitionId());
    check(restored.materializedTotals() == doc.materializedTotals(), "Restore recounts totals");
    checkTotals(restored, "restored");
    const auto snapshot = doc.readSnapshot();
    checkTotals(snapshot, "snapshot");
    // A locked instance cannot be restructured, whichever scheme validates it.
    check(rejection([&] { doc.move(locked, {1, 0, 0}); }) ==
              "Cannot edit a locked entity or its contents",
          "Locked placement stays locked");
    placeComponent(doc, definition, Transform::translation({0, 20, 0}));
    checkTotals(doc, "after locked checks");
}
} // namespace
int main() {
    try {
        Document::setFullValidationOracle(true);
        const auto incrementalBefore = Document::incrementalValidationCount();
        placementBoundary();
        bodyBoundary();
        restoreAndSnapshots();
        for (std::uint32_t seed : {0x82ffu, 0x1234u, 0xbeefu})
            randomizedEquivalence(seed, 220);
        check(Document::incrementalValidationCount() > incrementalBefore + 100,
              "The incremental scheme must be exercised under the oracle");
        std::cout << "incremental validations: "
                  << Document::incrementalValidationCount() - incrementalBefore << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
