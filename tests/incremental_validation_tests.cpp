// R082.ff: incremental edit validation must accept, reject, report and store exactly
// what full-document validation does. Every apply below runs with the full-validation
// oracle on, so each step is also validated by recounting the whole candidate.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "sanitizer_mode.hpp"
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
    // Unchanged loose geometry just below the unique vertex limit (299,520 of 300,000 since
    // R110) keeps sequences at the boundary without re-validating large records on every step. A
    // locked groups keep random erases from releasing it, so loose edits and unique copies reach
    // the unique limit. placementBoundary covers the materialized limits, which random sequences
    // would need about 1,250 placements to reach. Ballast is setup, not an edit under test; no
    // build runs the oracle while building it (every step of the sequences below does).
    Document::setFullValidationOracle(false);
    std::set<Id> ballast;
    for (int i = 0; i < 300; ++i) {
        std::vector<Vec3> loop;
        const int sides = i < 299 ? 1000 : 520;
        for (int j = 0; j < sides; ++j) {
            const auto angle = 2 * std::numbers::pi * j / sides;
            loop.push_back({50 + std::cos(angle), std::sin(angle), double(i)});
        }
        ballast.insert(doc.addFace({loop}, "Ballast"));
    }
    // Locked groups of 100 faces each. One group over all 300 faces would be one edit whose undo
    // record is charged to the history budget.
    for (auto next = ballast.begin(); next != ballast.end();) {
        std::set<Id> chunk;
        for (int n = 0; n < 100 && next != ballast.end(); ++n, ++next)
            chunk.insert(*next);
        setEntityState(doc, createGroup(doc, chunk, "Ballast"), {}, true);
    }
    Document::setFullValidationOracle(true);
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
        // Bias towards the large definition, whose unique copies press the limit.
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
    // R115: the unique limit is 2,400,000 vertices, far above the ballast (about 300,000, kept at
    // its R110 size for CI time), so random edits no longer reach it. The limits are checked by
    // counts in document_limits_tests; this sequence still covers acceptance, rejection and failed batches.
    check(accepted > steps / 4 && rejected > 0 && batchFailures > 0,
          "Randomized sequence did not cover acceptance, rejection and failed batches");
}

void placementBoundary() {
    // R115: the materialized vertex limit is 4,800,000 and the edge limit 7,200,000 (from 600,000
    // and 900,000). A 5000-sided prism instance materializes 10,000 vertices and 15,000 edges, so
    // 480 instances meet the vertex limit. That fill is built on the GPU PC (the R115 acceptance);
    // here the boundary is checked by counts, and ten instances run the placement, undo and redo
    // path under the full-validation oracle.
    static_assert(DocumentLimits::materialized.vertices == 4800000);
    static_assert(DocumentLimits::materialized.edges == 7200000);
    Document doc;
    const auto definition = createComponent(doc, prism(doc, 5000), "Boundary").definition;
    check(doc.materializedTotals().vertices == 10000, "Unexpected boundary fixture size");
    // The fill is setup and runs without the oracle (it recounts the whole document on every apply).
    Document::setFullValidationOracle(false);
    for (int i = 1; i < 10; ++i)
        placeComponent(doc, definition, Transform::translation({3.0 * i, 0, 0}));
    Document::setFullValidationOracle(true);
    check(doc.materializedTotals().vertices == 100000 && doc.materializedTotals().edges == 150000,
          "Ten instances of the fixture materialize 100,000 vertices and 150,000 edges");
    checkTotals(doc, "fixture");
    check(rejection([] {
              checkLimits({0, 0, DocumentLimits::materialized.vertices + 1}, LimitClass::Placement,
                          "Component placement", "placing definition 'Boundary' #1 into the model");
          }) == "Component placement exceeds document placement limits: vertices would be "
                "4,800,001 of 4,800,000 (placing definition 'Boundary' #1 into the model)",
          "One placement past the materialized vertex limit is rejected by name");
    check(rejection([] {
              checkLimits({0, 0, 0, 0, 0, DocumentLimits::materialized.edges + 1}, LimitClass::Placement,
                          "Component placement", "placing definition 'Boundary' #1 into the model");
          }) == "Component placement exceeds document placement limits: edges would be "
                "7,200,001 of 7,200,000 (placing definition 'Boundary' #1 into the model)",
          "One placement past the materialized edge limit is rejected by name");
    doc.undo();
    check(doc.materializedTotals().vertices == 90000, "Undo releases one placement");
    checkTotals(doc, "after undo");
    doc.redo();
    check(doc.materializedTotals().vertices == 100000, "Redo restores the fixture total");
    checkTotals(doc, "after redo");
}

void materializedBodyBoundary() {
    // Sanitized form of placementBoundary. A definition of a frame and 99 empty groups materializes
    // 100 bodies per placement, so the materialized body limit (320,000 at R115) is 3,200 placements.
    // That fill is built on the GPU PC; here the boundary is checked by counts, and a fixture of 4
    // placements runs the placement and undo path.
    static_assert(DocumentLimits::materialized.bodies == 320000);
    Document doc;
    const auto first = doc.nextId();
    Edit groups{"Groups", {}};
    std::set<Id> parts;
    for (Id i = 0; i < 99; ++i) {
        auto group = std::make_shared<Body>();
        group->id = first + i;
        group->kind = BodyKind::Group;
        group->name = "Part";
        groups.changes.push_back({group->id, nullptr, group});
        parts.insert(group->id);
    }
    doc.apply(std::move(groups), doc.revision());
    const auto definition =
        createComponent(doc, createGroup(doc, parts, "Boundary"), "Boundary").definition;
    check(doc.bodies().size() == 100, "Unexpected boundary fixture size");
    for (int i = 1; i < 4; ++i)
        placeComponent(doc, definition, Transform::translation({3.0 * i, 0, 0}));
    check(doc.materializedTotals().records == 400, "Four placements materialize 400 bodies");
    checkTotals(doc, "fixture");
    check(rejection([] {
              checkLimits({0, DocumentLimits::materialized.bodies + 1}, LimitClass::Placement,
                          "Component placement", "placing definition 'Boundary' #1 into the model");
          }) == "Component placement exceeds document placement limits: bodies would be "
                "320,001 of 320,000 (placing definition 'Boundary' #1 into the model)",
          "One placement past the materialized body limit is rejected by name");
    doc.undo();
    check(doc.materializedTotals().records == 300, "Undo releases one placement");
    checkTotals(doc, "after undo");
}

void bodyBoundary() {
    // A one-triangle definition materializes two records per instance (frame and geometry) but
    // counts its members once toward unique geometry. Loose empty groups fill a fixed allowance
    // here; the unique body limit (80,000 at R115) is checked by counts, and its fill is built on
    // the GPU PC.
    static_assert(DocumentLimits::unique.bodies == 80000);
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
    const auto definition = createComponent(doc, body, "Triangle").definition;
    const size_t stored = doc.definitions().at(definition)->members.size();
    constexpr size_t filled = 100;
    Edit fill{"Fill", {}};
    for (size_t i = 0; i < filled - stored; ++i) {
        auto group = std::make_shared<Body>();
        group->id = doc.nextId() + i;
        group->kind = BodyKind::Group;
        group->name = "Empty";
        fill.changes.push_back({group->id, nullptr, group});
    }
    doc.apply(std::move(fill), doc.revision());
    for (size_t i = 0; i < 6; ++i)
        placeComponent(doc, definition, Transform::translation({2.0 * i, 0, 0}));
    check(doc.bodies().size() == filled + 6 * stored,
          "Placements beyond the fixture's body allowance succeed");
    checkTotals(doc, "fixture body count");
    const auto extra = doc.nextId();
    check(rejection([&] {
              checkLimits({0, DocumentLimits::unique.bodies + 1}, LimitClass::UniqueGeometry,
                          "Document edit", "body #" + std::to_string(extra));
          }) == "Document edit exceeds document unique geometry limits: bodies would be "
                "80,001 of 80,000 (body #" +
                    std::to_string(extra) + ")",
          "One loose record past the unique body limit is rejected by name");
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
// R082.jj: a top-level placement root may change its transform and parent under
// the incremental scheme; every record it owns, through nested placements, is
// checked again. Each apply also runs the full scheme (oracle), which must agree
// on acceptance, the first error and the resulting state.
void placementRootMoves() {
    Document doc;
    const auto leaf = createComponent(doc, prism(doc, 6), "Leaf");
    const auto inner = placeComponent(doc, leaf.definition, Transform::translation({0, 5, 0}));
    const auto loose = prism(doc, 4, 1, {3, 5, 0});
    const auto nested = createComponent(doc, createGroup(doc, {inner.instance, loose}, "Outer"),
                                        "Nested");
    const auto other = placeComponent(doc, leaf.definition, Transform::translation({9, 0, 0}));
    const auto group = createGroup(doc, {prism(doc, 5, 1, {0, -9, 0})}, "Group");
    checkTotals(doc, "root moves setup");
    const auto nestedRoot = doc.instances().at(nested.instance)->members;
    Id nestedLeaf = 0, nestedMember = 0;
    for (const auto &[member, target] : nestedRoot)
        if (target != nested.instance)
            (doc.instances().contains(target) ? nestedLeaf : nestedMember) = target;
    check(nestedLeaf && nestedMember, "Nested fixture has a nested placement and a member");
    auto incremental = [&](const std::function<void()> &operation, const std::string &expected,
                           bool viaIncremental, const std::string &context) {
        const auto before = fingerprint(doc);
        const auto count = Document::incrementalValidationCount();
        const auto message = rejection(operation);
        check(message == expected, context + ": expected '" + expected + "', got '" + message + "'");
        check((Document::incrementalValidationCount() > count) == viaIncremental,
              context + ": unexpected validation scheme");
        if (!expected.empty())
            check(fingerprint(doc) == before, context + ": rejected edit changed the document");
        checkTotals(doc, context);
    };
    const auto parentOf = [&](Id id) { return doc.bodies().at(id)->parent; };
    const auto local = [&](Id id) { return doc.bodies().at(id)->transform; };
    // Moving the nested placement moves its nested placement and members.
    incremental([&] { doc.move(nested.instance, {2, 0, 1}); }, {}, true, "move nested root");
    incremental([&] { doc.transform(other.instance, local(other.instance), group); }, {}, true,
                "reparent root into a group");
    incremental([&] { doc.transform(other.instance, local(other.instance), 0); }, {}, true,
                "reparent root to the model");
    incremental([&] { setEntityState(doc, nested.instance, true, {}); }, {}, true,
                "hide a placement root");
    // Owned records leave the model bounds.
    incremental(
        [&] {
            doc.transform(nested.instance,
                          Transform::translation({999999.5, 0, 0}) * local(nested.instance),
                          parentOf(nested.instance));
        },
        "Coordinates must be finite and within one million meters", true, "root out of bounds");
    // A root moved into another placement's member is unbound geometry inside it.
    incremental([&] { doc.transform(other.instance, local(other.instance), nestedMember); },
                "Unbound geometry inside a component instance", true, "root into a placement");
    // A root moved under its own nested placement forms a cycle.
    incremental([&] { doc.transform(nested.instance, local(nested.instance), nestedLeaf); },
                "Cyclic or excessively deep hierarchy", true, "root under its own descendant");
    // A nested placement root is bound by its enclosing placement: full scheme.
    incremental([&] { doc.move(nestedLeaf, {1, 0, 0}); },
                "Resolved component member disagrees with its definition", false,
                "nested root move");
    // Undo and redo restore moved roots exactly.
    const auto moved = doc.worldTransform(nestedLeaf);
    doc.undo();
    doc.redo();
    check(doc.worldTransform(nestedLeaf) == moved, "Redo restores a moved nested placement");
    checkTotals(doc, "root moves undo/redo");
}
} // namespace
int main() {
    try {
        Document::setFullValidationOracle(true);
        const auto incrementalBefore = Document::incrementalValidationCount();
        // The serial sanitized CI job checks the materialized body limit instead
        // of the 600,000-vertex boundary, which is too slow under ASan.
        if constexpr (test::sanitized)
            materializedBodyBoundary();
        else
            placementBoundary();
        bodyBoundary();
        restoreAndSnapshots();
        placementRootMoves();
        // The serial sanitized CI job runs one randomized seed; unsanitized
        // builds run all three.
        const std::vector<std::uint32_t> seeds =
            test::sanitized ? std::vector<std::uint32_t>{0x82ffu}
                            : std::vector<std::uint32_t>{0x82ffu, 0x1234u, 0xbeefu};
        for (auto seed : seeds)
            randomizedEquivalence(seed, 120);
        std::cout << (test::sanitized
                          ? "sanitized mode: materialized body boundary, 1 seed of 120 steps\n"
                          : "full mode: materialized vertex/edge boundary, 3 seeds of 120 steps (R115: CI time)\n");
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
