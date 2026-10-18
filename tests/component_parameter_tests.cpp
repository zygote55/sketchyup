// R093.b: component parameter records, the dependency graph and regeneration
// (ADR 0165 §1, §3, §4).
//   component_parameter_tests [--print-golden]
#include "core/component_glue.hpp"
#include "core/component_parameters.hpp"
#include "core/component_regeneration.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/hosted_components.hpp"
#include "core/materials.hpp"
#include "core/transform_selection.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
using namespace sketchy;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
ComponentParameterError expectCode(const std::function<void()> &operation,
                                   const std::string &code, const std::string &label) {
    try {
        operation();
    } catch (const ComponentParameterError &error) {
        check(error.code() == code,
              label + ": expected " + code + ", got " + error.code() + " (" + error.what() + ")");
        check(std::strlen(error.what()) <= 256, label + ": message is at most 256 bytes");
        return error;
    } catch (const std::exception &error) {
        throw std::runtime_error(label + ": expected " + code + ", got: " + error.what());
    }
    throw std::runtime_error(label + ": expected " + code + ", but the operation succeeded");
}
// Final lengths are quantized to 1e-9 m (ADR 0165 §2); overrides store final values.
ParameterValue metres(double value) {
    return ParameterQuantity{quantizeParameterLength(value), {1, 0}};
}
ParameterValue number(double value) { return ParameterQuantity{value, {0, 0}}; }
double bodyMinX(const Body &body) {
    double result = 1e9;
    for (const auto &[id, point] : body.surface.vertices)
        result = std::min(result, point.x);
    return result;
}
double bodyMaxX(const Body &body) {
    double result = -1e9;
    for (const auto &[id, point] : body.surface.vertices)
        result = std::max(result, point.x);
    return result;
}
Id box(Document &doc, Vec3 low, Vec3 high, const std::string &name) {
    const auto id = doc.addFace(
        {{{low.x, low.y, low.z}, {high.x, low.y, low.z}, {high.x, high.y, low.z}, {low.x, high.y, low.z}}},
        name);
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, high.z - low.z);
    return id;
}
// The canonical record dump of a document: every scene record, definition (template
// and parameter record), binding (and overrides) and allocator, in ID order.
std::string dump(const Document &doc, bool geometryOnly = false, bool allocators = true) {
    std::string out;
    auto u64 = [&](std::uint64_t value) {
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    };
    for (const auto &[id, body] : doc.bodies())
        appendCanonicalBody(out, *body);
    if (geometryOnly)
        return out;
    for (const auto &[id, definition] : doc.definitions()) {
        u64(id);
        u64(definition->root);
        u64(definition->nextMemberId);
        out += definition->name;
        for (const auto &[member, body] : definition->members)
            appendCanonicalBody(out, *body);
        for (const auto &[member, target] : definition->references) {
            u64(member);
            u64(target);
        }
        if (definition->parameters)
            appendCanonicalParameters(out, *definition->parameters);
    }
    for (const auto &[root, instance] : doc.instances()) {
        u64(root);
        u64(instance->definition);
        for (const auto &[member, target] : instance->members) {
            u64(member);
            u64(target);
        }
        appendCanonicalOverrides(out, instance->overrides);
    }
    if (allocators) {
        u64(doc.nextId());
        u64(doc.nextDefinitionId());
        u64(doc.nextComponentDriverId());
    }
    return out;
}
std::string digest(const Document &doc, bool geometryOnly = false) {
    return digestHex(sha256Digest(dump(doc, geometryOnly)));
}
// ADR 0165 §1: after every committed edit the variant at the defaults equals the
// template, except for repeat copies.
void invariant(const Document &doc, const std::string &label) {
    ComponentVariantEngine engine(doc.definitions());
    for (const auto &[id, definition] : doc.definitions()) {
        if (!definition->parameters)
            continue;
        const auto variant = engine.variant(id, {});
        if (!variant)
            continue;
        for (const auto &[member, body] : definition->members)
            check(*variant->members.at(member) == *body,
                  label + ": the variant at the defaults equals the template (member #" +
                      std::to_string(member) + ")");
        for (const auto &[member, body] : variant->members)
            check(definition->members.contains(member) || variant->generated.contains(member),
                  label + ": the default variant adds only repeat copies");
    }
    // Every placement projects its variant, and the index matches a recount.
    check(sameComponentVariants(*computeComponentVariants(doc.definitions(), doc.instances()),
                                doc.componentVariants()),
          label + ": the variant index matches a recount");
    doc.verifyIncrementalState();
}
// A batch as the command layer composes it: operations on a staged snapshot, then
// one edit and one history entry (src/automation/commands.cpp).
void batch(Document &doc, const std::string &label,
           const std::vector<std::function<void(Document &)>> &operations) {
    Document staged = doc.readSnapshot();
    for (const auto &operation : operations)
        operation(staged);
    Edit edit{label, {}};
    appendSceneMetadataChanges(edit, doc, staged);
    std::set<Id> ids;
    for (const auto &[id, body] : doc.bodies())
        ids.insert(id);
    for (const auto &[id, body] : staged.bodies())
        ids.insert(id);
    for (const auto id : ids) {
        const auto before = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        const auto after = staged.bodies().contains(id) ? staged.bodies().at(id) : nullptr;
        if (before != after)
            edit.changes.push_back({id, before, after, {}, {}, {}, true});
    }
    edit.nextIdFloor = staged.nextId();
    doc.apply(std::move(edit), doc.revision());
}
// Every model record table, by pointer.
struct Records {
    std::map<Id, BodyPtr> bodies;
    ComponentDefinitions definitions;
    ComponentInstances instances;
    HostedPtr hosted;
    ComponentVariantsPtr variants;
    std::vector<Id> allocators;
    std::uint64_t revision{};
    std::size_t history{}, historyBytes{};
    bool dirty{};
    explicit Records(const Document &doc)
        : bodies(doc.bodies()), definitions(doc.definitions()), instances(doc.instances()),
          hosted(doc.hostedRecords()), variants(doc.componentVariantRecords()),
          allocators{doc.nextId(), doc.nextDefinitionId(), doc.nextComponentDriverId()},
          revision(doc.revision()), history(doc.history().total),
          historyBytes(doc.historyBytes()), dirty(doc.dirty()) {}
    bool operator==(const Records &) const = default;
};
// A batch whose last operation fails leaves revision, history, dirty state and every
// record pointer-identical (ADR 0165 §4).
ComponentParameterError failsClosed(Document &doc, const std::string &code,
                                    const std::string &label,
                                    const std::vector<std::function<void(Document &)>> &operations) {
    const Records before(doc);
    const auto error = expectCode([&] { batch(doc, label, operations); }, code, label);
    check(Records(doc) == before, label + ": every record is pointer-identical and unchanged");
    return error;
}

// ---- The acceptance fixture (ADR 0165 §1) ----
struct Table {
    Document doc;
    Id definition{}, placement{};
    Id top{}, legLeftFront{}, legLeftBack{}, legRightFront{}, legRightBack{}, shelf{};
    Id width{}, legFront{}, legBack{}, repeat{}, visibility{};
};
ComponentParameterSpec spec(std::string name, std::string label, ParameterType type,
                            ComponentParameterMode mode, std::string expression,
                            std::optional<double> min = {}, std::optional<double> max = {}) {
    ComponentParameterSpec result;
    result.name = std::move(name);
    result.label = std::move(label);
    result.type = type;
    result.mode = mode;
    result.expression = std::move(expression);
    result.min = min;
    result.max = max;
    return result;
}
ComponentDriverSpec driver(ComponentDriverKind kind, Id member, std::string expression,
                           ComponentAxis axis = ComponentAxis::X) {
    ComponentDriverSpec result;
    result.kind = kind;
    result.member = member;
    result.expression = std::move(expression);
    result.axis = axis;
    result.maxCount = 0;
    return result;
}
void tableBodies(Table &t) {
    auto &doc = t.doc;
    t.top = box(doc, {0, 0, .72}, {.9, .6, .75}, "Top");
    t.legLeftFront = box(doc, {0, 0, 0}, {.05, .05, .72}, "Leg left front");
    t.legLeftBack = box(doc, {0, .55, 0}, {.05, .6, .72}, "Leg left back");
    t.legRightFront = box(doc, {.84, 0, 0}, {.89, .05, .72}, "Leg right front");
    t.legRightBack = box(doc, {.84, .55, 0}, {.89, .6, .72}, "Leg right back");
    t.shelf = box(doc, {.05, .05, .2}, {.84, .55, .22}, "Shelf");
    const auto group = createGroup(
        doc, {t.top, t.legLeftFront, t.legLeftBack, t.legRightFront, t.legRightBack, t.shelf},
        "Table");
    const auto created = createComponent(doc, group, "Table");
    t.definition = created.definition;
    t.placement = created.instance;
}
// width (900 mm, 0.4–2.4), shelves (1, 0–4), derived leg_x = width - 60 mm; a stretch
// on the top, positions on the right legs, a repeat and a visibility on the shelf.
Table makeTable(bool shuffled = false) {
    Table t;
    tableBodies(t);
    auto &doc = t.doc;
    const auto width = spec("width", "Width", ParameterType::Length, ComponentParameterMode::Input,
                            "900mm", .4, 2.4);
    const auto shelves = spec("shelves", "Shelves", ParameterType::Count,
                              ComponentParameterMode::Input, "1", 0, 4);
    const auto legX = spec("leg_x", "Leg position", ParameterType::Length,
                           ComponentParameterMode::Derived, "width-60 mm");
    auto repeatSpec = driver(ComponentDriverKind::Repeat, t.shelf, "max(shelves,1)",
                             ComponentAxis::Z);
    repeatSpec.maxCount = 4;
    repeatSpec.spacing = "300 mm";
    auto stretch = driver(ComponentDriverKind::Stretch, t.top, "width");
    stretch.at = .45;
    if (!shuffled) {
        defineComponentParameter(doc, t.definition, width);
        defineComponentParameter(doc, t.definition, shelves);
        defineComponentParameter(doc, t.definition, legX);
        t.width = driveComponent(doc, t.definition, 0, stretch).createdDrivers.at(0);
        t.legFront = driveComponent(doc, t.definition, 0,
                                    driver(ComponentDriverKind::Position, t.legRightFront, "leg_x"))
                         .createdDrivers.at(0);
        t.legBack = driveComponent(doc, t.definition, 0,
                                   driver(ComponentDriverKind::Position, t.legRightBack, "leg_x"))
                        .createdDrivers.at(0);
        t.repeat = driveComponent(doc, t.definition, 0, repeatSpec).createdDrivers.at(0);
        t.visibility = driveComponent(doc, t.definition, 0,
                                      driver(ComponentDriverKind::Visibility, t.shelf,
                                             "shelves > 0"))
                           .createdDrivers.at(0);
    } else {
        // Another insertion order: the same records up to display order and driver IDs.
        defineComponentParameter(doc, t.definition, shelves);
        defineComponentParameter(doc, t.definition, width, 0);
        defineComponentParameter(doc, t.definition, legX);
        t.visibility = driveComponent(doc, t.definition, 0,
                                      driver(ComponentDriverKind::Visibility, t.shelf,
                                             "shelves > 0"))
                           .createdDrivers.at(0);
        t.repeat = driveComponent(doc, t.definition, 0, repeatSpec).createdDrivers.at(0);
        t.legBack = driveComponent(doc, t.definition, 0,
                                   driver(ComponentDriverKind::Position, t.legRightBack, "leg_x"))
                        .createdDrivers.at(0);
        t.legFront = driveComponent(doc, t.definition, 0,
                                    driver(ComponentDriverKind::Position, t.legRightFront, "leg_x"))
                         .createdDrivers.at(0);
        t.width = driveComponent(doc, t.definition, 0, stretch).createdDrivers.at(0);
    }
    invariant(doc, "table fixture");
    return t;
}
Id sceneMember(const Document &doc, Id placement, Id member) {
    return doc.instances().at(placement)->members.at(member);
}
const Body &sceneBody(const Document &doc, Id placement, Id member) {
    return *doc.bodies().at(sceneMember(doc, placement, member));
}
Vec3 worldOrigin(const Document &doc, Id placement, Id member) {
    return doc.worldTransform(sceneMember(doc, placement, member)).point({});
}
} // namespace
namespace {
void digests() {
    check(digestHex(sha256Digest("")) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 of the empty string");
    check(digestHex(sha256Digest("abc")) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of abc");
    check(digestHex(sha256Digest(std::string(1000, 'a'))) ==
              "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
          "SHA-256 across several blocks");
    std::cout << "digests: ok\n";
}
// Overriding width 0.9 -> 1.2 m stretches the top to 1.2 m and moves only the right
// legs by 0.3 m; the template and the left legs are unchanged.
void tableWidth() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto definition = doc.definitions().at(t.definition);
    const auto leftFront = doc.bodies().at(sceneMember(doc, t.placement, t.legLeftFront));
    const auto leftBack = doc.bodies().at(sceneMember(doc, t.placement, t.legLeftBack));
    const auto rightBefore = worldOrigin(doc, t.placement, t.legRightFront);
    const auto history = doc.history().total;
    const auto result = overrideComponentParameters(doc, t.placement, {{"width", metres(1.2)}});
    invariant(doc, "width override");
    check(doc.history().total == history + 1, "An override is one history entry");
    const auto &top = sceneBody(doc, t.placement, t.top);
    check(std::abs(bodyMaxX(top) - 1.2) < 1e-12 && bodyMinX(top) == 0,
          "The top is stretched to 1.2 m");
    const auto rightAfter = worldOrigin(doc, t.placement, t.legRightFront);
    check(std::abs(rightAfter.x - rightBefore.x - .3) < 1e-12 && rightAfter.y == rightBefore.y &&
              rightAfter.z == rightBefore.z,
          "The right front leg moves 0.3 m along x");
    check(std::abs(worldOrigin(doc, t.placement, t.legRightBack).x - rightBefore.x - .3) < 1e-12,
          "The right back leg moves 0.3 m along x");
    check(doc.bodies().at(sceneMember(doc, t.placement, t.legLeftFront)) == leftFront &&
              doc.bodies().at(sceneMember(doc, t.placement, t.legLeftBack)) == leftBack,
          "The left legs are unchanged (pointer-identical records)");
    check(doc.definitions().at(t.definition) == definition,
          "The template is unchanged (pointer-identical definition)");
    check(result.regenerated.placements == 1 && result.regenerated.variants == 1,
          "The result reports one regenerated placement and one new variant");
    check(doc.instances().at(t.placement)->overrides.at("width") == metres(1.2),
          "The override is stored as a quantized SI value");
    // An undo restores the exact records.
    doc.undo();
    invariant(doc, "width undo");
    check(bodyMaxX(sceneBody(doc, t.placement, t.top)) == .9 &&
              doc.componentVariant(t.placement) == nullptr,
          "Undo restores the template placement");
    doc.redo();
    invariant(doc, "width redo");
    check(std::abs(bodyMaxX(sceneBody(doc, t.placement, t.top)) - 1.2) < 1e-12,
          "Redo restores the variant placement");
    std::cout << "table width: ok\n";
}
// shelves 0–4 hides or repeats the shelf; copies keep stable scene IDs while they
// exist, and rising counts allocate new IDs above the floor that are never reused.
void shelvesRepeat() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto block = doc.definitions().at(t.definition)->parameters->drivers;
    Id first = 0;
    for (const auto &d : block)
        if (d.id == t.repeat)
            first = d.firstMember;
    check(first >= t.shelf && doc.definitions().at(t.definition)->nextMemberId == first + 3,
          "The repeat reserves (maxCount - 1) member IDs below nextMemberId");
    auto copies = [&] {
        std::vector<Id> result;
        const auto &binding = *doc.instances().at(t.placement);
        for (Id member = first; member < first + 3; ++member)
            if (binding.members.contains(member))
                result.push_back(binding.members.at(member));
        return result;
    };
    overrideComponentParameters(doc, t.placement, {{"shelves", number(0)}});
    invariant(doc, "shelves 0");
    check(sceneBody(doc, t.placement, t.shelf).hidden && copies().empty(),
          "shelves 0 hides the shelf and has no copies");
    overrideComponentParameters(doc, t.placement, {{"shelves", number(1)}});
    invariant(doc, "shelves 1");
    check(!sceneBody(doc, t.placement, t.shelf).hidden && copies().empty(),
          "shelves 1 shows one shelf");
    overrideComponentParameters(doc, t.placement, {{"shelves", number(4)}});
    invariant(doc, "shelves 4");
    const auto four = copies();
    check(four.size() == 3, "shelves 4 makes three copies");
    const auto base = worldOrigin(doc, t.placement, t.shelf);
    for (std::size_t k = 0; k < four.size(); ++k) {
        const auto origin = doc.worldTransform(four[k]).point({});
        check(std::abs(origin.z - base.z - .3 * double(k + 1)) < 1e-12 && origin.x == base.x,
              "Copy k is offset by k * 300 mm along z");
        check(*doc.bodies().at(four[k]) != sceneBody(doc, t.placement, t.shelf) &&
                  doc.bodies().at(four[k])->surface == sceneBody(doc, t.placement, t.shelf).surface,
              "Copies share the shelf's geometry");
    }
    const auto floor = doc.nextId();
    overrideComponentParameters(doc, t.placement, {{"shelves", number(2)}});
    invariant(doc, "shelves 2");
    const auto two = copies();
    check(two.size() == 1 && two[0] == four[0] && !doc.bodies().contains(four[1]) &&
              !doc.bodies().contains(four[2]),
          "A falling count keeps the first copy's ID and retires the others");
    overrideComponentParameters(doc, t.placement, {{"shelves", number(3)}});
    invariant(doc, "shelves 3");
    const auto three = copies();
    check(three.size() == 2 && three[0] == four[0] && three[1] >= floor &&
              three[1] != four[1] && three[1] != four[2],
          "A rising count allocates a new ID above the floor and never reuses a retired one");
    // Visibility applies to the copies too.
    overrideComponentParameters(doc, t.placement, {{"shelves", number(0)}});
    check(copies().empty() && sceneBody(doc, t.placement, t.shelf).hidden, "Back to no shelves");
    expectCode([&] { overrideComponentParameters(doc, t.placement, {{"shelves", number(5)}}); },
               "PARAMETER_DOMAIN", "shelves above its maximum");
    const auto generated = expectCode(
        [&] {
            driveComponent(doc, t.definition, 0,
                           driver(ComponentDriverKind::Visibility, first, "shelves > 1"));
        },
        "PARAMETER_DRIVER_TARGET", "driving a generated repeat copy");
    check(std::string(generated.what()).find("generated") != std::string::npos,
          "A generated copy is named as such");
    // A default of zero shelves hides the template shelf; an override shows it again.
    ComponentParameterUpdate none;
    none.expression = "0";
    updateComponentParameter(doc, t.definition, "shelves", none);
    invariant(doc, "shelves default 0");
    check(doc.definitions().at(t.definition)->members.at(t.shelf)->hidden,
          "A visibility default of false hides the template member");
    overrideComponentParameters(doc, t.placement, {{"shelves", number(2)}});
    invariant(doc, "shelves shown over a hidden default");
    check(!sceneBody(doc, t.placement, t.shelf).hidden && copies().size() == 1,
          "An override shows the shelf a hidden default hides");
    // A new visibility driver whose default is false hides its member at once.
    defineComponentParameter(doc, t.definition,
                             spec("legs", "Legs", ParameterType::Boolean,
                                  ComponentParameterMode::Input, "false"));
    driveComponent(doc, t.definition, 0,
                   driver(ComponentDriverKind::Visibility, t.legLeftFront, "legs"));
    invariant(doc, "new hidden driver");
    check(doc.definitions().at(t.definition)->members.at(t.legLeftFront)->hidden &&
              sceneBody(doc, t.placement, t.legLeftFront).hidden,
          "A new visibility driver with a false default re-baselines the template");
    expectCode([&] { overrideComponentParameters(doc, t.placement, {{"shelves", number(2.5)}}); },
               "PARAMETER_DOMAIN", "a non-whole count");
    std::cout << "shelves repeat: ok\n";
}
// Identical in dev and sanitize; regenerate with --print-golden only on purpose.
std::string goldenDigest() {
    return "64f78ccba287b01c57d9cc337ca6d0764c32a878f404d6ce8332e48eba5def6f";
}
void determinism(bool print) {
    auto build = [](bool shuffled) {
        auto t = makeTable(shuffled);
        overrideComponentParameters(t.doc, t.placement,
                                    {{"width", metres(1.2)}, {"shelves", number(3)}});
        return t;
    };
    const auto reference = build(false);
    const auto bytes = dump(reference.doc);
    const auto hash = digestHex(sha256Digest(bytes));
    if (print)
        std::cout << "golden " << hash << '\n';
    for (int i = 0; i < 10; ++i)
        check(dump(build(false).doc) == bytes, "The canonical encoding is identical across runs");
    check(hash == goldenDigest() || print,
          "The canonical encoding matches the golden digest shared by dev and sanitize (got " +
              hash + ")");
    // Another insertion order of parameters and drivers gives the same geometry.
    check(digest(build(true).doc, true) == digest(reference.doc, true),
          "Parameter and driver insertion order does not change the geometry");
    // The same edit with its records in shuffled order gives the same bytes.
    auto t = makeTable();
    const auto before = t.doc.readSnapshot();
    overrideComponentParameters(t.doc, t.placement,
                                {{"width", metres(1.2)}, {"shelves", number(3)}});
    Edit edit{"Shuffled", {}};
    appendSceneMetadataChanges(edit, before, t.doc);
    for (const auto &[id, body] : t.doc.bodies())
        if (!before.bodies().contains(id) || before.bodies().at(id) != body)
            edit.changes.push_back({id, before.bodies().contains(id) ? before.bodies().at(id) : nullptr,
                                    body, {}, {}, {}, true});
    for (const auto &[id, body] : before.bodies())
        if (!t.doc.bodies().contains(id))
            edit.changes.push_back({id, body, nullptr, {}, {}, {}, true});
    edit.nextIdFloor = t.doc.nextId();
    const auto expected = dump(t.doc);
    for (unsigned seed = 1; seed <= 5; ++seed) {
        auto candidate = before.readSnapshot();
        auto shuffled = edit;
        std::mt19937 random(seed);
        std::shuffle(shuffled.changes.begin(), shuffled.changes.end(), random);
        std::shuffle(shuffled.instances.begin(), shuffled.instances.end(), random);
        std::shuffle(shuffled.definitions.begin(), shuffled.definitions.end(), random);
        candidate.apply(std::move(shuffled), candidate.revision());
        check(dump(candidate) == expected, "Shuffled record order gives identical bytes");
    }
    // Undo and redo restore the exact bytes.
    const auto after = dump(t.doc);
    t.doc.undo();
    // Allocator floors never rewind (ADR 0165 §4); every record is restored exactly.
    check(dump(t.doc, false, false) == dump(before, false, false) &&
              t.doc.nextId() >= before.nextId(),
          "Undo restores the records before the edit");
    t.doc.redo();
    check(dump(t.doc) == after, "Redo restores the bytes after the edit");
    // Restoring the records (the load path) gives the same bytes and variants.
    Document restored;
    restored.restore(t.doc.identity(), t.doc.nextId(), t.doc.bodies(), t.doc.revision(),
                     t.doc.definitions(), t.doc.instances(), t.doc.nextDefinitionId(),
                     t.doc.tags(), t.doc.nextTagId(), t.doc.materials(), t.doc.nextMaterialId(),
                     t.doc.assets(), t.doc.nextAssetId(), t.doc.displayUnits(),
                     t.doc.hostedRecords(), t.doc.style(), t.doc.scenes(), t.doc.nextSceneId(),
                     t.doc.sections(), t.doc.nextSectionId(), t.doc.activeSections(),
                     t.doc.annotations(), t.doc.nextAnnotationId(), t.doc.solar(),
                     t.doc.displayPrecision(), {}, 1, {}, 1, {}, 1, t.doc.nextComponentDriverId());
    check(dump(restored) == after &&
              sameComponentVariants(restored.componentVariants(), t.doc.componentVariants()),
          "Restore validates and regenerates the same records");
    std::cout << "determinism: ok\n";
}
} // namespace
namespace {
// Changing a default re-baselines the template and regenerates every placement.
void rebaseline() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto a = t.placement;
    const auto b = placeComponent(doc, t.definition, Transform::translation({3, 0, 0})).instance;
    const auto c = placeComponent(doc, t.definition, Transform::translation({6, 0, 0})).instance;
    overrideComponentParameters(doc, a, {{"width", metres(1.2)}});
    overrideComponentParameters(doc, c, {{"width", metres(.6)}});
    const auto legTemplate =
        doc.definitions().at(t.definition)->members.at(t.legRightFront)->transform;
    ComponentParameterUpdate update;
    update.expression = "1200 mm";
    const auto result = updateComponentParameter(doc, t.definition, "width", update);
    invariant(doc, "default change");
    const auto &definition = *doc.definitions().at(t.definition);
    check(std::abs(bodyMaxX(*definition.members.at(t.top)) - 1.2) < 1e-12,
          "The template top is rewritten to the new default");
    check(std::abs(definition.members.at(t.legRightFront)->transform.m[12] - legTemplate.m[12] -
                   .3) < 1e-12,
          "The template right legs move to the new default");
    check(result.regenerated.placements >= 3, "Every placement regenerates");
    check(doc.componentVariant(a) == nullptr &&
              std::abs(bodyMaxX(sceneBody(doc, a, t.top)) - 1.2) < 1e-12,
          "A placement whose override equals the new default shows the template");
    check(std::abs(bodyMaxX(sceneBody(doc, b, t.top)) - 1.2) < 1e-12,
          "A default placement follows the new default");
    check(std::abs(bodyMaxX(sceneBody(doc, c, t.top)) - .6) < 1e-12,
          "An overriding placement keeps its value");
    for (const auto &d : definition.parameters->drivers)
        check(componentDriverState(definition, doc.definitions(), d) == ComponentDriverState::Bound,
              "A re-baseline restamps the drivers it moved");
    // Two stretches on one member: the upper plane moves by the lower one's delta.
    defineComponentParameter(doc, t.definition,
                             spec("lip", "Lip", ParameterType::Length,
                                  ComponentParameterMode::Input, "0 mm"));
    auto upper = driver(ComponentDriverKind::Stretch, t.top, "lip");
    upper.at = 1.0;
    const auto lip = driveComponent(doc, t.definition, 0, upper).createdDrivers.at(0);
    ComponentParameterUpdate grow;
    grow.expression = "1300 mm";
    updateComponentParameter(doc, t.definition, "width", grow);
    invariant(doc, "second default change");
    for (const auto &d : doc.definitions().at(t.definition)->parameters->drivers)
        if (d.id == lip)
            check(std::abs(d.at - 1.1) < 1e-12, "The upper stretch plane moves with the lower Δ");
    // A default that would move geometry across its own stretch plane is refused.
    ComponentParameterUpdate shrink;
    shrink.expression = "400 mm";
    expectCode([&] { updateComponentParameter(doc, t.definition, "width", shrink); },
               "PARAMETER_REGENERATION", "a re-baseline that crosses a stretch plane");
    // Repeated re-baselining stays valid; undo restores exact records.
    const auto bytes = dump(doc);
    ComponentParameterUpdate back;
    back.expression = "900 mm";
    updateComponentParameter(doc, t.definition, "width", back);
    invariant(doc, "default back");
    doc.undo();
    check(dump(doc) == bytes, "Undo restores the exact records after a re-baseline");
    std::cout << "re-baseline: ok\n";
}
// Placements with equal values share one variant object and its member records;
// unchanged members are the template's own records.
void sharing() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto a = t.placement;
    const auto b = placeComponent(doc, t.definition, Transform::translation({3, 0, 0})).instance;
    const auto c = placeComponent(doc, t.definition, Transform::translation({6, 0, 0})).instance;
    check(doc.componentVariant(b) == nullptr,
          "A default placement shows the template (no variant)");
    overrideComponentParameters(doc, a, {{"width", metres(1.2)}});
    overrideComponentParameters(doc, b, {{"width", metres(1.2)}, {"shelves", number(1)}});
    overrideComponentParameters(doc, c, {{"width", metres(1)}});
    invariant(doc, "sharing");
    const auto va = doc.componentVariant(a), vb = doc.componentVariant(b),
               vc = doc.componentVariant(c);
    check(va && va == vb, "Equal values share one variant object");
    check(vc && vc != va, "Different values have different variants");
    const auto &templateMembers = doc.definitions().at(t.definition)->members;
    check(va->members.at(t.legLeftFront) == templateMembers.at(t.legLeftFront) &&
              va->members.at(t.shelf) == templateMembers.at(t.shelf),
          "Undriven members are the template's shared records");
    check(va->members.at(t.top) != templateMembers.at(t.top) &&
              va->members.at(t.top) == vb->members.at(t.top),
          "Driven members are shared between placements of one variant");
    check(doc.componentVariants().variants.size() == 2,
          "The document holds two distinct variants");
    // Sharing survives unrelated edits and undo/redo.
    doc.move(c, {0, 1, 0});
    check(doc.componentVariant(a) == va && doc.componentVariant(b) == va,
          "An unrelated edit keeps the shared variant objects");
    const auto copy = transformSelected(doc, {{a}}, Transform::translation({0, 3, 0}), {},
                                        TransformSpace::World, true);
    const auto copied = copy.copies.at(a);
    invariant(doc, "copy");
    check(doc.instances().at(copied)->overrides == doc.instances().at(a)->overrides &&
              doc.componentVariant(copied) == va,
          "Copying a placement copies its overrides and shares its variant");
    std::cout << "variant sharing: ok\n";
}
// A parametric assembly drives a nested placement through childParameter.
struct Room {
    Table table;
    Id definition{}, placement{}, nested{}, drivenBy{};
};
Room makeRoom() {
    Room r{makeTable()};
    auto &doc = r.table.doc;
    const auto floor = box(doc, {-1, -1, -.1}, {3, 3, 0}, "Floor");
    overrideComponentParameters(doc, r.table.placement, {{"width", metres(1.1)}});
    const auto group = createGroup(doc, {r.table.placement, floor}, "Room");
    expectCode([&] { createComponent(doc, group, "Room"); }, "INVALID_TARGET",
               "nesting a placement with overrides");
    resetComponentParameters(doc, r.table.placement);
    const auto created = createComponent(doc, group, "Room");
    r.definition = created.definition;
    r.placement = created.instance;
    r.nested = r.table.placement;
    defineComponentParameter(doc, r.definition,
                             spec("table_width", "Table width", ParameterType::Length,
                                  ComponentParameterMode::Input, "1500 mm"));
    auto child = driver(ComponentDriverKind::ChildParameter, r.nested, "table_width");
    child.axis = ComponentAxis::X;
    child.parameter = "width";
    r.drivenBy = driveComponent(doc, r.definition, 0, child).createdDrivers.at(0);
    invariant(doc, "room");
    return r;
}
void nested() {
    auto r = makeRoom();
    auto &doc = r.table.doc;
    // The nested placement's root ID is a member of the room's definition.
    check(std::abs(bodyMaxX(sceneBody(doc, r.nested, r.table.top)) - 1.5) < 1e-12,
          "The room's default drives the nested table's width");
    check(componentPlacementInputs(doc, r.nested).at("width") == metres(1.5),
          "The nested placement's values come from its parent");
    overrideComponentParameters(doc, r.placement, {{"table_width", metres(1.8)}});
    invariant(doc, "room override");
    check(std::abs(bodyMaxX(sceneBody(doc, r.nested, r.table.top)) - 1.8) < 1e-12,
          "Overriding the room drives the nested table");
    expectCode([&] { overrideComponentParameters(doc, r.nested, {{"width", metres(1)}}); },
               "INVALID_TARGET", "a nested placement holds no overrides");
    expectCode([&] { overrideComponentParameters(doc, r.placement, {{"table_width", metres(3)}}); },
               "PARAMETER_DOMAIN", "a driven child value outside the child's bounds");
    // Exploding the room keeps the nested table's variant as its overrides.
    const auto before = sceneBody(doc, r.nested, r.table.top);
    explodeGroup(doc, r.placement);
    invariant(doc, "explode");
    check(doc.instances().at(r.nested)->overrides.at("width") == metres(1.8) &&
              sceneBody(doc, r.nested, r.table.top).surface == before.surface,
          "Exploding gives the variant's records; nested values become overrides");
    // A child parameter that is still driven cannot be removed; renaming follows.
    doc.undo();
    expectCode([&] { removeComponentParameter(doc, r.table.definition, "width"); },
               "PARAMETER_NAME", "removing a driven child parameter");
    renameComponentParameter(doc, r.table.definition, "width", "span");
    invariant(doc, "rename");
    for (const auto &d : doc.definitions().at(r.definition)->parameters->drivers)
        check(d.parameter == "span" &&
                  componentDriverState(*doc.definitions().at(r.definition), doc.definitions(), d) ==
                      ComponentDriverState::Bound,
              "Renaming rewrites and restamps the parent's childParameter driver");
    check(std::abs(bodyMaxX(sceneBody(doc, r.nested, r.table.top)) - 1.8) < 1e-12,
          "Renaming keeps every value");
    std::cout << "nested childParameter: ok\n";
}
// A glued cutting component with a driven width re-cuts its host in the same edit.
void hosted() {
    Document doc;
    auto wall = doc.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
    doc.extrude(wall, doc.bodies().at(wall)->surface.faces.begin()->first, 1);
    Id top = 0;
    for (const auto &[id, face] : doc.bodies().at(wall)->surface.faces)
        if (doc.bodies().at(wall)->surface.normal(id).z > .99)
            top = id;
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    const auto reference = doc.bodies().at(body)->surface.faces.begin()->first;
    const auto window = createComponent(doc, body, "Window");
    const auto member = window.movedGeometry.at(body);
    setComponentGlue(doc, window.definition, ComponentGlue{member, reference, {1, 1, 0}, {1, 0, 0}, true});
    defineComponentParameter(doc, window.definition,
                             spec("width", "Width", ParameterType::Length,
                                  ComponentParameterMode::Input, "2 m"));
    defineComponentParameter(doc, window.definition,
                             spec("lift", "Lift", ParameterType::Length,
                                  ComponentParameterMode::Input, "0 m"));
    auto stretch = driver(ComponentDriverKind::Stretch, member, "width");
    stretch.at = 1;
    driveComponent(doc, window.definition, 0, stretch);
    expectCode(
        [&] {
            driveComponent(doc, window.definition, 0,
                           driver(ComponentDriverKind::Position, member, "lift", ComponentAxis::Z));
        },
        "PARAMETER_DRIVER_TARGET", "a position along the glue normal");
    expectCode(
        [&] {
            defineComponentParameter(doc, window.definition,
                                     spec("tilt", "Tilt", ParameterType::Angle,
                                          ComponentParameterMode::Input, "0 deg"));
            driveComponent(doc, window.definition, 0,
                           driver(ComponentDriverKind::Rotation, member, "tilt", ComponentAxis::X));
        },
        "PARAMETER_DRIVER_TARGET", "a rotation of the glue member");
    attachComponent(doc, window.instance, wall, top, {{3, 3, 1}});
    auto span = [&] {
        const auto &corners =
            doc.hostedComponents().hosts.at(wall)->openings.at(window.instance).profile.corners;
        double low = 1e9, high = -1e9;
        for (const auto &corner : corners) {
            low = std::min(low, corner.point.x);
            high = std::max(high, corner.point.x);
        }
        return high - low;
    };
    check(std::abs(span() - 2) < 1e-9, "The opening starts 2 m wide");
    const auto wallBefore = doc.bodies().at(wall);
    const auto history = doc.history().total;
    overrideComponentParameters(doc, window.instance, {{"width", metres(3)}});
    invariant(doc, "hosted override");
    check(doc.history().total == history + 1 && doc.bodies().at(wall) != wallBefore &&
              std::abs(span() - 3) < 1e-9,
          "The driven width re-cuts the host opening in the same edit");
    doc.undo();
    check(doc.bodies().at(wall) == wallBefore && std::abs(span() - 2) < 1e-9,
          "Undo restores the host and its opening");
    doc.redo();
    // A stretch that moves the glue face off its plane is refused on regeneration.
    auto lift = driver(ComponentDriverKind::Stretch, member, "lift", ComponentAxis::Z);
    lift.at = -1;
    driveComponent(doc, window.definition, 0, lift);
    const Records before(doc);
    expectCode([&] { overrideComponentParameters(doc, window.instance, {{"lift", metres(.1)}}); },
               "PARAMETER_REGENERATION", "a stretch that moves the glue face off its plane");
    check(Records(doc) == before, "The refused regeneration leaves the document unchanged");
    std::cout << "hosted re-cut: ok\n";
}
} // namespace
namespace {
// For each error code, a batch whose last operation fails changes nothing.
void failClosed() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto other = placeComponent(doc, t.definition, Transform::translation({3, 0, 0})).instance;
    defineComponentParameter(doc, t.definition,
                             spec("overhang", "Overhang", ParameterType::Length,
                                  ComponentParameterMode::Input, "0 m"));
    auto overhang = driver(ComponentDriverKind::Stretch, t.top, "overhang");
    overhang.at = .45;
    driveComponent(doc, t.definition, 0, overhang);
    doc.markSaved();
    const auto first = [&](Document &draft) {
        overrideComponentParameters(draft, other, {{"width", metres(1)}});
    };
    auto define = [&](std::string name, std::string expression,
                      ParameterType type = ParameterType::Length) {
        return [=, &t](Document &draft) {
            defineComponentParameter(draft, t.definition,
                                     spec(name, "Label", type, ComponentParameterMode::Derived,
                                          expression));
        };
    };
    auto syntax = failsClosed(doc, "PARAMETER_SYNTAX", "syntax", {first, define("bad", "width +")});
    check(syntax.context().offset >= 1 && syntax.context().parameter == "bad",
          "A syntax error names the parameter and a 1-based offset");
    failsClosed(doc, "PARAMETER_NAME", "unknown name", {first, define("bad", "depth * 2")});
    failsClosed(doc, "PARAMETER_NAME", "reserved name", {first, define("sin", "1 m")});
    failsClosed(doc, "PARAMETER_TYPE", "type", {first, define("bad", "45 deg")});
    failsClosed(doc, "PARAMETER_DOMAIN", "domain",
                {first, [&](Document &draft) {
                     overrideComponentParameters(draft, t.placement, {{"width", metres(2.5)}});
                 }});
    failsClosed(doc, "PARAMETER_DOMAIN", "division by zero",
                {first, define("ratio", "1 m / (width - width)", ParameterType::Number)});
    failsClosed(doc, "PARAMETER_CYCLE", "cycle",
                {first, define("a", "1 m"), define("b", "a + 1 m"), [&](Document &draft) {
                     ComponentParameterUpdate update;
                     update.expression = "b";
                     updateComponentParameter(draft, t.definition, "a", update);
                 }});
    failsClosed(doc, "PARAMETER_DRIVER_TARGET", "driver target",
                {first, [&](Document &draft) {
                     driveComponent(draft, t.definition, 0,
                                    driver(ComponentDriverKind::Position, 999999, "width"));
                 }});
    failsClosed(doc, "PARAMETER_DRIVER_TARGET", "root target",
                {first, [&](Document &draft) {
                     driveComponent(draft, t.definition, 0,
                                    driver(ComponentDriverKind::Visibility,
                                           draft.definitions().at(t.definition)->root, "true"));
                 }});
    failsClosed(doc, "PARAMETER_REGENERATION", "a flipped face",
                {first, [&](Document &draft) {
                     overrideComponentParameters(draft, t.placement, {{"overhang", metres(-1)}});
                 }});
    failsClosed(doc, "PARAMETER_TEMPLATE_SCOPE", "template scope",
                {first, [&](Document &draft) { requireComponentTemplateScope(draft, other); }});
    failsClosed(doc, "LIMIT_EXCEEDED", "expression limit",
                {first, define("long", std::string(1025, '1'))});
    failsClosed(doc, "LOCKED_ENTITY", "locked placement",
                {first, [&](Document &draft) {
                     setEntityState(draft, t.placement, {}, true);
                 },
                 [&](Document &draft) {
                     overrideComponentParameters(draft, t.placement, {{"width", metres(1.3)}});
                 }});
    failsClosed(doc, "NOT_FOUND", "unknown definition",
                {first, [&](Document &draft) {
                     defineComponentParameter(draft, 999999,
                                              spec("x", "X", ParameterType::Length,
                                                   ComponentParameterMode::Input, "1 m"));
                 }});
    failsClosed(doc, "INVALID_TARGET", "parameter position",
                {first, [&](Document &draft) {
                     defineComponentParameter(draft, t.definition,
                                              spec("x", "X", ParameterType::Length,
                                                   ComponentParameterMode::Input, "1 m"),
                                              99);
                 }});
    check(!doc.dirty(), "Refused batches leave the saved state clean");
    std::cout << "fail closed: ok\n";
}
// A failure in one placement of many refuses the whole edit and names it.
void onePlacementFails() {
    auto t = makeTable();
    auto &doc = t.doc;
    std::vector<Id> placements{t.placement};
    for (int i = 1; i < 6; ++i)
        placements.push_back(
            placeComponent(doc, t.definition, Transform::translation({3.0 * i, 0, 0})).instance);
    for (std::size_t i = 0; i < placements.size(); ++i)
        overrideComponentParameters(doc, placements[i], {{"width", metres(1.0 + .1 * double(i))}});
    const Records before(doc);
    // ratio divides by zero only where width is 1.3 m.
    const auto error = expectCode(
        [&] {
            defineComponentParameter(doc, t.definition,
                                     spec("ratio", "Ratio", ParameterType::Number,
                                          ComponentParameterMode::Derived,
                                          "1 m / (width - 1300 mm)"));
        },
        "PARAMETER_DOMAIN", "one failing placement");
    check(error.context().placement == placements[3] &&
              std::string(error.what()).find(std::to_string(placements[3])) != std::string::npos,
          "The error names the failing placement");
    check(Records(doc) == before, "One failing placement refuses the edit with no change");
    std::cout << "one placement fails: ok\n";
}
// A cycle names up to eight of its parameters, in dependency order.
void cycles() {
    auto t = makeTable();
    auto &doc = t.doc;
    defineComponentParameter(doc, t.definition,
                             spec("p9", "P", ParameterType::Length, ComponentParameterMode::Input,
                                  "1 m"));
    for (int i = 8; i >= 0; --i)
        defineComponentParameter(doc, t.definition,
                                 spec("p" + std::to_string(i), "P", ParameterType::Length,
                                      ComponentParameterMode::Input,
                                      "p" + std::to_string(i + 1) + " + 1 m"));
    ComponentParameterUpdate update;
    update.expression = "p0 + 1 m";
    const auto error = expectCode(
        [&] { updateComponentParameter(doc, t.definition, "p9", update); }, "PARAMETER_CYCLE",
        "a ten-parameter cycle");
    check(error.context().cycle ==
              std::vector<std::string>{"p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7"},
          "The cycle names eight parameters in order");
    ComponentParameterUpdate self;
    self.expression = "p9 * 2";
    const auto own = expectCode([&] { updateComponentParameter(doc, t.definition, "p9", self); },
                                "PARAMETER_CYCLE", "a self-reference");
    check(own.context().cycle == std::vector<std::string>{"p9"}, "A self-reference is a cycle");
    std::cout << "cycles: ok\n";
}
// Locked placements refuse overrides and shared edits that would change them.
void locks() {
    auto t = makeTable();
    auto &doc = t.doc;
    setEntityState(doc, t.placement, {}, true);
    expectCode([&] { overrideComponentParameters(doc, t.placement, {{"width", metres(1)}}); },
               "LOCKED_ENTITY", "an override on a locked placement");
    expectCode([&] { resetComponentParameters(doc, t.placement); }, "LOCKED_ENTITY",
               "a reset on a locked placement");
    ComponentParameterUpdate update;
    update.expression = "1 m";
    expectCode([&] { updateComponentParameter(doc, t.definition, "width", update); },
               "LOCKED_ENTITY", "a default change that regenerates a locked placement");
    ComponentParameterUpdate label;
    label.label = "Table width";
    updateComponentParameter(doc, t.definition, "width", label);
    invariant(doc, "label on locked");
    check(doc.definitions().at(t.definition)->parameters->parameters.front().label == "Table width",
          "A shared edit that leaves the locked placement unchanged is allowed");
    std::cout << "locks: ok\n";
}
} // namespace
namespace {
// make-unique keeps overrides under new driver IDs; replace drops them (reported);
// edit-instance promotes the values to the unique copy's defaults.
void uniqueReplacePromote() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto other = placeComponent(doc, t.definition, Transform::translation({3, 0, 0})).instance;
    overrideComponentParameters(doc, t.placement, {{"width", metres(1.2)}, {"shelves", number(3)}});
    expectCode([&] { promoteComponentParameters(doc, t.placement); }, "INVALID_TARGET",
               "promoting a placement whose definition is shared");
    const auto bodies = doc.bodies();
    const auto oldDrivers = doc.definitions().at(t.definition)->parameters->drivers;
    const auto allocator = doc.nextComponentDriverId();
    const auto unique = makeComponentUnique(doc, t.placement);
    invariant(doc, "make unique");
    const auto &copy = *doc.definitions().at(unique.definition);
    check(unique.definition != t.definition && copy.parameters &&
              copy.parameters->drivers.size() == oldDrivers.size() &&
              unique.createdDrivers.size() == oldDrivers.size(),
          "Make-unique copies the parameter record");
    for (std::size_t i = 0; i < oldDrivers.size(); ++i)
        check(copy.parameters->drivers[i].id >= allocator &&
                  copy.parameters->drivers[i].kind == oldDrivers[i].kind &&
                  copy.parameters->drivers[i].bound == oldDrivers[i].bound,
              "Make-unique gives the copied drivers new IDs and keeps their state");
    check(doc.instances().at(t.placement)->overrides.at("width") == metres(1.2) &&
              doc.bodies() == bodies,
          "Make-unique keeps the overrides and every scene record");
    // Promotion: the placement's values become the unique definition's defaults.
    const auto promoted = promoteComponentParameters(doc, t.placement);
    invariant(doc, "promotion");
    const auto &record = *doc.definitions().at(unique.definition)->parameters;
    check(promoted.parametersPromoted && doc.instances().at(t.placement)->overrides.empty(),
          "Promotion reports parametersPromoted and clears the overrides");
    check(record.parameters[0].expression == "1.2 m" &&
              record.parameters[1].expression == "3",
          "The values become literal defaults (" + record.parameters[0].expression + ", " +
              record.parameters[1].expression + ")");
    check(doc.bodies() == bodies, "Promotion keeps every scene record");
    check(std::abs(bodyMaxX(*doc.definitions().at(unique.definition)->members.at(t.top)) - 1.2) <
              1e-12,
          "The unique template is re-baselined to the promoted values");
    requireComponentTemplateScope(doc, t.placement);
    // Naming a repeat copy through the placement is refused; a template member is not.
    const auto &binding = *doc.instances().at(t.placement);
    Id copyRecord = 0;
    for (const auto &[member, target] : binding.members)
        if (doc.componentVariant(t.placement)->generated.contains(member))
            copyRecord = target;
    check(copyRecord != 0, "Promoted defaults keep the shelf copies");
    expectCode([&] { requireComponentTemplateScope(doc, t.placement, {copyRecord}); },
               "PARAMETER_TEMPLATE_SCOPE", "naming a repeat copy");
    requireComponentTemplateScope(doc, t.placement, {sceneMember(doc, t.placement, t.top)});
    // The original definition and its other placement are untouched.
    check(doc.instances().at(other)->definition == t.definition &&
              bodyMaxX(sceneBody(doc, other, t.top)) == .9,
          "Other placements keep the original definition");
    // A template edit through the promoted placement works in template coordinates.
    const auto edit = editComponentDefinition(
        doc, unique.definition,
        [&](Document &draft) {
            draft.move(t.legLeftFront, {0, 0, .01});
            return ChangeReport{};
        },
        doc.worldTransform(t.placement));
    invariant(doc, "template edit");
    check(edit.parameterWarnings.empty(), "Moving an undriven member warns about no driver");
    // Replace drops the overrides and reports them.
    overrideComponentParameters(doc, other, {{"width", metres(1.1)}});
    const auto plain = createComponent(doc, box(doc, {5, 5, 0}, {6, 6, 1}, "Crate"), "Crate");
    const auto replaced = replaceComponent(doc, other, plain.definition);
    invariant(doc, "replace");
    check(replaced.droppedOverrides == std::vector<std::string>{"width"} &&
              doc.instances().at(other)->overrides.empty(),
          "Replace drops the overrides and reports them");
    std::cout << "make unique, promotion, replace: ok\n";
}
// Template edits: a broken driver target refuses unless the same edit or batch
// retargets or removes the driver; bound and changedSinceBound.
void templateEdits() {
    auto t = makeTable();
    auto &doc = t.doc;
    for (const auto &d : doc.definitions().at(t.definition)->parameters->drivers)
        check(componentDriverState(*doc.definitions().at(t.definition), doc.definitions(), d) ==
                  ComponentDriverState::Bound,
              "Drivers are bound when created");
    auto eraseShelf = [&](Document &draft) {
        draft.erase(t.shelf);
        return ChangeReport{};
    };
    expectCode([&] { editComponentDefinition(doc, t.definition, eraseShelf); },
               "PARAMETER_DRIVER_TARGET", "deleting a driver target");
    expectCode(
        [&] {
            editComponentDefinition(doc, t.definition, [&](Document &draft) {
                undriveComponent(draft, t.definition, t.repeat);
                return eraseShelf(draft);
            });
        },
        "INVALID_TARGET", "a parameter operation inside the definition's own scope");
    // A curve that crosses a stretch plane is refused; one beside it is not.
    auto circle = [&](Vec3 center) {
        return [&, center](Document &draft) {
            return draft.addCurve(t.top, centerCurve(CurveKind::Circle,
                                                     DrawingPlane::make(center, {0, 0, 1}, {1, 0, 0}),
                                                     .05, 0, 2 * 3.141592653589793, 24));
        };
    };
    expectCode([&] { editComponentDefinition(doc, t.definition, circle({.45, .3, .75})); },
               "PARAMETER_DRIVER_TARGET", "a curve crossing a stretch plane");
    const auto curved = editComponentDefinition(doc, t.definition, circle({.2, .3, .75}));
    invariant(doc, "curve beside the plane");
    check(curved.parameterWarnings == std::vector<Id>{t.width},
          "Adding a curve to the stretched top warns about its driver");
    rebindComponentDrivers(doc, t.definition);
    invariant(doc, "rebind all");
    overrideComponentParameters(doc, t.placement, {{"width", metres(1.4)}});
    invariant(doc, "curve stretch");
    // The same edit may remove the drivers whose target it deletes.
    auto withBatch = doc.readSnapshot();
    const auto removed = editComponentDefinition(
        doc, t.definition, eraseShelf, {},
        [&](const ComponentDefinition &, const ComponentParametersPtr &current) {
            auto next = *current;
            std::erase_if(next.drivers, [&](const auto &d) { return d.member == t.shelf; });
            return std::make_shared<const ComponentParameters>(std::move(next));
        });
    invariant(doc, "edit with driver removal");
    check(!doc.definitions().at(t.definition)->members.contains(t.shelf) &&
              doc.definitions().at(t.definition)->parameters->drivers.size() == 3,
          "A template edit can remove the drivers of the target it deletes");
    // A batch that removes the drivers first is one entry.
    const auto entries = withBatch.history().total;
    batch(withBatch, "Remove shelf",
          {[&](Document &draft) { undriveComponent(draft, t.definition, t.repeat); },
           [&](Document &draft) { undriveComponent(draft, t.definition, t.visibility); },
           [&](Document &draft) { editComponentDefinition(draft, t.definition, eraseShelf); }});
    invariant(withBatch, "batch removal");
    check(withBatch.history().total == entries + 1 &&
              !withBatch.definitions().at(t.definition)->members.contains(t.shelf),
          "Undrive and template edit compose into one history entry");
    // Changing a driver's target makes it changedSinceBound and the edit reports it.
    const auto moved = editComponentDefinition(doc, t.definition, [&](Document &draft) {
        draft.move(t.top, {0, 0, .01});
        return ChangeReport{};
    });
    invariant(doc, "moved top");
    check(moved.parameterWarnings == std::vector<Id>{t.width},
          "A template edit lists the drivers it moved to changedSinceBound");
    auto state = [&](Id id) {
        for (const auto &d : doc.definitions().at(t.definition)->parameters->drivers)
            if (d.id == id)
                return componentDriverState(*doc.definitions().at(t.definition), doc.definitions(),
                                            d);
        throw std::runtime_error("missing driver");
    };
    check(state(t.width) == ComponentDriverState::ChangedSinceBound &&
              state(t.legFront) == ComponentDriverState::Bound,
          "Driver state is derived from the stored digest");
    rebindComponentDrivers(doc, t.definition, {t.width});
    check(state(t.width) == ComponentDriverState::Bound, "Rebind stamps the driver again");
    expectCode([&] { rebindComponentDrivers(doc, t.definition, {999999}); }, "NOT_FOUND",
               "rebinding an unknown driver");
    std::cout << "template edits and driver state: ok\n";
}
// D-038: a paint inside the definition changes no shape, so the drivers bound to the
// painted member stay bound. Their stored digest follows the new appearance; any other
// change to the target still makes them changedSinceBound.
void paintKeepsDriversBound() {
    auto t = makeTable();
    auto &doc = t.doc;
    auto record = [&] { return doc.definitions().at(t.definition)->parameters; };
    auto driverOf = [&](Id id) -> ComponentDriver {
        for (const auto &d : record()->drivers)
            if (d.id == id)
                return d;
        throw std::runtime_error("missing driver");
    };
    auto state = [&](Id id) {
        return componentDriverState(*doc.definitions().at(t.definition), doc.definitions(),
                                    driverOf(id));
    };
    auto allBound = [&] {
        return std::all_of(record()->drivers.begin(), record()->drivers.end(), [&](const auto &d) {
            return componentDriverState(*doc.definitions().at(t.definition), doc.definitions(),
                                        d) == ComponentDriverState::Bound;
        });
    };
    const auto opened = record();
    // A colour on the stretched top.
    const auto painted = editComponentDefinition(doc, t.definition, [&](Document &draft) {
        draft.paint(t.top, {.4f, .25f, .1f});
        return ChangeReport{};
    });
    invariant(doc, "painted top");
    check(doc.definitions().at(t.definition)->members.at(t.top)->color ==
              std::array<float, 3>{.4f, .25f, .1f},
          "The paint reaches the template");
    check(painted.parameterWarnings.empty() && allBound(),
          "Painting a driven member leaves every driver bound");
    check(driverOf(t.width).bound != [&] {
        for (const auto &d : opened->drivers)
            if (d.id == t.width)
                return d.bound;
        return ComponentDigest{};
    }(), "The painted member's driver is restamped over its new colour");
    for (const auto &d : opened->drivers)
        if (d.id != t.width)
            check(driverOf(d.id).bound == d.bound, "Drivers on unpainted members keep their digest");
    // A material on the shelf: the repeat (its subtree) and visibility drivers stay bound.
    const auto oak = createMaterial(doc, "Oak", {.6f, .45f, .3f});
    const auto material = editComponentDefinition(doc, t.definition, [&](Document &draft) {
        return assignMaterial(draft, t.shelf, std::nullopt, oak);
    });
    invariant(doc, "material on shelf");
    check(doc.definitions().at(t.definition)->members.at(t.shelf)->materials.front == oak,
          "The material reaches the template");
    check(material.parameterWarnings.empty() && state(t.repeat) == ComponentDriverState::Bound &&
              state(t.visibility) == ComponentDriverState::Bound && allBound(),
          "A material on a repeated member leaves its drivers bound");
    // Undo (assignment, material, paint) restores the exact records, digests included.
    doc.undo();
    doc.undo();
    doc.undo();
    check(*record() == *opened, "Undo restores the stored digests");
    check(allBound(), "Undo leaves every driver bound");
    doc.redo();
    doc.redo();
    doc.redo();
    check(allBound(), "Redo leaves every driver bound");
    // Moving the member still counts, with or without a paint in the same edit.
    const auto both = editComponentDefinition(doc, t.definition, [&](Document &draft) {
        draft.paint(t.top, {.1f, .2f, .3f});
        draft.move(t.top, {0, 0, .01});
        return ChangeReport{};
    });
    invariant(doc, "painted and moved top");
    check(both.parameterWarnings == std::vector<Id>{t.width} &&
              state(t.width) == ComponentDriverState::ChangedSinceBound,
          "A paint with a move marks the driver changed");
    // A paint does not clear a driver that was already changed.
    const auto stored = driverOf(t.width).bound;
    editComponentDefinition(doc, t.definition, [&](Document &draft) {
        draft.paint(t.top, {.7f, .7f, .7f});
        return ChangeReport{};
    });
    invariant(doc, "repainted changed top");
    check(state(t.width) == ComponentDriverState::ChangedSinceBound &&
              driverOf(t.width).bound == stored,
          "A paint keeps a changed driver changed and its stored digest");
    rebindComponentDrivers(doc, t.definition, {t.width});
    const auto moved = editComponentDefinition(doc, t.definition, [&](Document &draft) {
        draft.move(t.shelf, {0, 0, .01});
        return ChangeReport{};
    });
    invariant(doc, "moved shelf");
    check(moved.parameterWarnings == std::vector<Id>{t.repeat, t.visibility} &&
              state(t.width) == ComponentDriverState::Bound,
          "Moving the shelf still marks its drivers changed");
    std::cout << "paint keeps drivers bound: ok\n";
}
} // namespace
namespace {
struct Panel {
    Document doc;
    Id definition{}, placement{}, member{};
};
Panel makePanel() {
    Panel p;
    const auto face = p.doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, "Panel");
    const auto created = createComponent(p.doc, face, "Panel");
    p.definition = created.definition;
    p.placement = created.instance;
    p.member = created.movedGeometry.at(face);
    return p;
}
// An expression of exactly `bytes` canonical bytes: terms joined by " + ".
std::string sized(std::size_t bytes) {
    static const std::vector<std::string> lasts{"",       "1",        "12",        "1.5",
                                                "1.25",   "1.125",    "1.0625",    "1.03125",
                                                "1.015625", "1.0078125", "1.00390625"};
    std::string text;
    while (bytes - text.size() > 14)
        text += (text.empty() ? "" : " + ") + std::string("1.015625");
    const auto rest = bytes - text.size() - (text.empty() ? 0 : 3);
    text += (text.empty() ? "" : " + ") + lasts.at(rest);
    return text;
}
// "-x + x + ... + x" with `terms` names: 2 * terms nodes.
std::string chain(std::size_t terms, const std::string &name = "x") {
    std::string text = "-" + name;
    for (std::size_t i = 1; i < terms; ++i)
        text += " + " + name;
    return text;
}
void expressionLimits() {
    auto p = makePanel();
    auto &doc = p.doc;
    auto define = [&](const std::string &name, const std::string &expression,
                      ParameterType type = ParameterType::Number) {
        defineComponentParameter(doc, p.definition,
                                 spec(name, "Limit", type, ComponentParameterMode::Input, expression));
    };
    check(canonicalParameterExpression(parseParameterExpression(sized(1024))).size() == 1024,
          "The text fixture is 1,024 canonical bytes");
    define("text_n", sized(1024));
    expectCode([&] { define("text_n1", sized(1025)); }, "LIMIT_EXCEEDED", "1,025 expression bytes");
    define("x", "1");
    define("nodes_n", chain(128));
    expectCode([&] { define("nodes_n1", "-" + chain(128)); }, "LIMIT_EXCEEDED", "257 nodes");
    define("depth_n", std::string(32, '(') + "1" + std::string(32, ')'));
    expectCode([&] { define("depth_n1", std::string(33, '(') + "1" + std::string(33, ')')); },
               "LIMIT_EXCEEDED", "depth 33");
    define("literal_n", "1." + std::string(30, '0'));
    expectCode([&] { define("literal_n1", "1." + std::string(31, '0')); }, "PARAMETER_SYNTAX",
               "a 33-character number literal");
    // 512 tokens (256 nodes, parentheses add tokens only), then 513.
    std::string tokens = "+(1)";
    for (int i = 1; i < 128; ++i)
        tokens += " + (1)";
    check(parseParameterExpression(tokens).tokenCount == 512, "The token fixture has 512 tokens");
    define("tokens_n", tokens);
    expectCode([&] { define("tokens_n1", "(" + tokens + ")"); }, "LIMIT_EXCEEDED", "514 tokens");
    std::string many = "1";
    for (int i = 0; i < 256; ++i)
        many += "+1";
    expectCode([&] { define("tokens_n2", many); }, "LIMIT_EXCEEDED", "513 tokens");
    invariant(doc, "expression limits");
    std::cout << "expression limits: ok\n";
}
void recordLimits() {
    {
        auto p = makePanel();
        for (int i = 0; i < 128; ++i)
            defineComponentParameter(p.doc, p.definition,
                                     spec("q" + std::to_string(i), "Q", ParameterType::Number,
                                          ComponentParameterMode::Input, "1"));
        expectCode(
            [&] {
                defineComponentParameter(p.doc, p.definition,
                                         spec("q128", "Q", ParameterType::Number,
                                              ComponentParameterMode::Input, "1"));
            },
            "LIMIT_EXCEEDED", "the 129th parameter");
        invariant(p.doc, "128 parameters");
    }
    {
        auto p = makePanel();
        auto choice = spec("pick", "Pick", ParameterType::Choice, ComponentParameterMode::Input,
                           "\"o0\"");
        for (int i = 0; i < 32; ++i)
            choice.options.push_back({"o" + std::to_string(i), "Option"});
        defineComponentParameter(p.doc, p.definition, choice);
        choice.name = "pick2";
        choice.options.push_back({"o32", "Option"});
        expectCode([&] { defineComponentParameter(p.doc, p.definition, choice); }, "LIMIT_EXCEEDED",
                   "33 choice options");
    }
    {
        auto p = makePanel();
        defineComponentParameter(p.doc, p.definition,
                                 spec("on", "On", ParameterType::Boolean,
                                      ComponentParameterMode::Input, "true"));
        for (int i = 0; i < 256; ++i)
            driveComponent(p.doc, p.definition, 0,
                           driver(ComponentDriverKind::Visibility, p.member, "on"));
        expectCode(
            [&] {
                driveComponent(p.doc, p.definition, 0,
                               driver(ComponentDriverKind::Visibility, p.member, "on"));
            },
            "LIMIT_EXCEEDED", "the 257th driver");
        overrideComponentParameters(p.doc, p.placement, {{"on", false}});
        invariant(p.doc, "256 visibility drivers");
        check(sceneBody(p.doc, p.placement, p.member).hidden,
              "Visibility drivers on one member combine");
    }
    std::cout << "record limits: ok\n";
}
} // namespace
namespace {
Document rebuilt(const Document &source, std::map<Id, BodyPtr> bodies,
                 ComponentInstances instances, Id next, ComponentDefinitions definitions) {
    Document doc;
    doc.restore(source.identity(), next, std::move(bodies), source.revision(), std::move(definitions),
                std::move(instances), source.nextDefinitionId(), source.tags(), source.nextTagId(),
                source.materials(), source.nextMaterialId(), source.assets(), source.nextAssetId(),
                source.displayUnits(), source.hostedRecords(), source.style(), source.scenes(),
                source.nextSceneId(), source.sections(), source.nextSectionId(),
                source.activeSections(), source.annotations(), source.nextAnnotationId(),
                source.solar(), source.displayPrecision(), {}, 1, {}, 1, {}, 1,
                source.nextComponentDriverId());
    return doc;
}
// Copies of a one-member placement with the given overrides, each projected through
// its variant (or copied from the template when `project` is false).
void addPlacements(const Document &source, Id prototype, const std::vector<ComponentOverrides> &values,
                   const ComponentDefinitions &definitions, std::map<Id, BodyPtr> &bodies,
                   ComponentInstances &instances, Id &next, bool project) {
    const auto &binding = *source.instances().at(prototype);
    const auto &definition = *definitions.at(binding.definition);
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto instance = std::make_shared<ComponentInstance>(binding);
        instance->overrides = values[i];
        std::map<Id, Id> remap;
        for (auto &[member, target] : instance->members) {
            remap[target] = next;
            target = next++;
        }
        auto root = std::make_shared<Body>(*source.bodies().at(prototype));
        root->id = instance->members.at(definition.root);
        root->transform = Transform::translation({2.0 * double(i % 100), 2.0 * double(i / 100), 0});
        instances[root->id] = instance;
        if (project) {
            for (auto &[id, body] : projectComponentPlacement(definitions, instances, root->id, *root))
                bodies[id] = body;
            continue;
        }
        bodies[root->id] = root;
        for (const auto &[member, target] : binding.members) {
            if (target == prototype)
                continue;
            auto body = std::make_shared<Body>(*source.bodies().at(target));
            body->id = remap.at(target);
            body->parent = remap.at(body->parent);
            bodies[body->id] = body;
        }
    }
}
DefinitionPtr withRecord(const DefinitionPtr &definition, ComponentParameters record) {
    for (auto &parameter : record.parameters)
        parameter.expression =
            canonicalParameterExpression(parseParameterExpression(parameter.expression));
    auto copy = std::make_shared<ComponentDefinition>(*definition);
    copy->parameters = std::make_shared<const ComponentParameters>(std::move(record));
    return copy;
}
ComponentParameter numberParameter(std::string name, std::string expression,
                                   ComponentParameterMode mode = ComponentParameterMode::Derived) {
    ComponentParameter parameter;
    parameter.name = std::move(name);
    parameter.label = "N";
    parameter.type = ParameterType::Number;
    parameter.mode = mode;
    parameter.expression = std::move(expression);
    return parameter;
}
// Document-level limits: 20,000,000 evaluation visits and 4,096 distinct variants,
// each accepted at n and refused at n + 1 before the work.
void documentLimits() {
    const auto started = std::chrono::steady_clock::now();
    {
        auto p = makePanel();
        auto &doc = p.doc;
        const auto second = createComponent(
            doc, doc.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}}, "Other"), "Other");
        ComponentParameters heavy;
        heavy.parameters.push_back(numberParameter("x", "1", ComponentParameterMode::Input));
        for (int i = 1; i <= 124; ++i)
            heavy.parameters.push_back(numberParameter("q" + std::to_string(i), chain(128)));
        ComponentParameters filler;
        filler.parameters.push_back(numberParameter("x", "1", ComponentParameterMode::Input));
        for (int i = 1; i <= 5; ++i) {
            std::string text = "x";
            for (int j = 1; j < 128; ++j)
                text += " + x";
            filler.parameters.push_back(numberParameter("r" + std::to_string(i), text));
        }
        for (int i = 1; i <= 3; ++i)
            filler.parameters.push_back(numberParameter("s" + std::to_string(i), "1"));
        auto definitions = doc.definitions();
        definitions[p.definition] = withRecord(definitions.at(p.definition), heavy);
        definitions[second.definition] = withRecord(definitions.at(second.definition), filler);
        // 31,745 + 1,279 (defaults) + 629 * 31,744 (distinct overrides) = 20,000,000.
        std::vector<ComponentOverrides> values;
        for (int k = 2; k <= 630; ++k)
            values.push_back({{"x", number(k)}});
        auto bodies = doc.bodies();
        auto instances = doc.instances();
        Id next = doc.nextId();
        addPlacements(doc, p.placement, values, definitions, bodies, instances, next, false);
        auto full = rebuilt(doc, bodies, instances, next, definitions);
        check(full.componentVariants().visits == ComponentParameterLimits::documentVisits,
              "A document at exactly 20,000,000 evaluation visits loads (got " +
                  std::to_string(full.componentVariants().visits) + ")");
        const auto error = expectCode(
            [&] { overrideComponentParameters(full, p.placement, {{"x", number(1000)}}); },
            "LIMIT_EXCEEDED", "one more distinct evaluation");
        check(std::string(error.what()).find("20,000,000") != std::string::npos,
              "The document evaluation budget names its limit");
        values.push_back({{"x", number(1000)}});
        bodies = doc.bodies();
        instances = doc.instances();
        next = doc.nextId();
        addPlacements(doc, p.placement, values, definitions, bodies, instances, next, false);
        expectCode([&] { (void)rebuilt(doc, bodies, instances, next, definitions); },
                   "LIMIT_EXCEEDED", "loading a document over the evaluation budget");
    }
    {
        auto p = makePanel();
        auto &doc = p.doc;
        defineComponentParameter(doc, p.definition,
                                 spec("w", "W", ParameterType::Length,
                                      ComponentParameterMode::Input, "1 m"));
        auto stretch = driver(ComponentDriverKind::Stretch, p.member, "w");
        stretch.at = .5;
        driveComponent(doc, p.definition, 0, stretch);
        std::vector<ComponentOverrides> values;
        for (int k = 1; k <= 4096; ++k)
            values.push_back({{"w", metres(1 + .001 * k)}});
        auto bodies = doc.bodies();
        auto instances = doc.instances();
        Id next = doc.nextId();
        addPlacements(doc, p.placement, values, doc.definitions(), bodies, instances, next, true);
        auto full = rebuilt(doc, bodies, instances, next, doc.definitions());
        check(full.componentVariants().variants.size() == ComponentParameterLimits::variants,
              "A document with 4,096 distinct variants loads");
        expectCode([&] { overrideComponentParameters(full, p.placement, {{"w", metres(.75)}}); },
                   "LIMIT_EXCEEDED", "the 4,097th variant");
        overrideComponentParameters(full, p.placement, {{"w", metres(1.001)}});
        check(full.componentVariant(p.placement) ==
                  full.componentVariant(full.instances().rbegin()->first) ||
                  full.componentVariants().variants.size() == ComponentParameterLimits::variants,
              "An existing variant is shared at the limit");
    }
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "document limits: ok (" << seconds << " s)\n";
}
void definitionLimits() {
    // Repeat maxCount 1,000 and 4,096 reserved member IDs.
    {
        Document doc;
        const auto a = box(doc, {0, 0, 0}, {.1, .1, .1}, "A");
        const auto b = box(doc, {.2, 0, 0}, {.3, .1, .1}, "B");
        const auto c = box(doc, {.4, 0, 0}, {.5, .1, .1}, "C");
        const auto single = box(doc, {0, 1, 0}, {.1, 1.1, .1}, "Single");
        const auto group = createGroup(doc, {a, b, c}, "Triple");
        const auto all = createGroup(doc, {group, single}, "Parts");
        const auto created = createComponent(doc, all, "Parts");
        defineComponentParameter(doc, created.definition,
                                 spec("n", "N", ParameterType::Count,
                                      ComponentParameterMode::Input, "1"));
        auto repeat = driver(ComponentDriverKind::Repeat, single, "n", ComponentAxis::Y);
        repeat.spacing = "1 m";
        repeat.maxCount = 1001;
        expectCode([&] { driveComponent(doc, created.definition, 0, repeat); }, "LIMIT_EXCEEDED",
                   "repeat maxCount 1,001");
        repeat.maxCount = 101;
        const auto small = driveComponent(doc, created.definition, 0, repeat).createdDrivers.at(0);
        auto big = driver(ComponentDriverKind::Repeat, group, "n", ComponentAxis::X);
        big.spacing = "1 m";
        big.maxCount = 1000; // 999 copies of a four-member subtree: 3,996 IDs
        driveComponent(doc, created.definition, 0, big);
        invariant(doc, "4,096 reserved IDs");
        repeat.maxCount = 102;
        expectCode([&] { driveComponent(doc, created.definition, small, repeat); },
                   "LIMIT_EXCEEDED", "4,097 reserved member IDs");
        overrideComponentParameters(doc, created.instance, {{"n", number(3)}});
        invariant(doc, "repeat with a subtree");
        check(doc.instances().at(created.instance)->members.size() ==
                  doc.definitions().at(created.definition)->members.size() + 2 * 4 + 2 * 1,
              "Copies of a subtree bind every copied member");
        expectCode([&] { overrideComponentParameters(doc, created.instance, {{"n", number(102)}}); },
                   "PARAMETER_DOMAIN", "a repeat count above maxCount");
    }
    // Parametric nesting along one reference path: 8 levels, then refused.
    {
        auto p = makePanel();
        auto &doc = p.doc;
        Id placement = p.placement, definition = p.definition;
        for (int level = 1; level <= 9; ++level) {
            if (level > 1) {
                const auto group = createGroup(doc, {placement}, "Level");
                const auto created = createComponent(doc, group, "Level " + std::to_string(level));
                placement = created.instance;
                definition = created.definition;
            }
            const auto define = [&] {
                defineComponentParameter(doc, definition,
                                         spec("v", "V", ParameterType::Number,
                                              ComponentParameterMode::Input, "1"));
            };
            if (level <= 8)
                define();
            else
                expectCode(define, "LIMIT_EXCEEDED", "a ninth parametric level");
        }
        invariant(doc, "eight parametric levels");
    }
    // Parametric definitions per document (the general 1,024-definition cap makes
    // this unreachable through edits, so the record check is exercised directly).
    {
        auto root = std::make_shared<Body>();
        root->id = 1;
        root->kind = BodyKind::Group;
        root->name = "Root";
        ComponentParameters record;
        record.parameters.push_back(numberParameter("a", "1", ComponentParameterMode::Input));
        auto shared = std::make_shared<const ComponentParameters>(record);
        ComponentDefinitions definitions;
        for (Id id = 1; id <= 1025; ++id) {
            auto definition = std::make_shared<ComponentDefinition>();
            definition->id = id;
            definition->root = 1;
            definition->nextMemberId = 2;
            definition->members[1] = root;
            definition->parameters = shared;
            definitions[id] = definition;
            if (id == 1024)
                validateComponentParameterRecords(definitions, nullptr, 1);
        }
        expectCode([&] { validateComponentParameterRecords(definitions, nullptr, 1); },
                   "LIMIT_EXCEEDED", "1,025 parametric definitions");
    }
    // Parameter records in accounting: 4 MiB, then refused.
    {
        auto root = std::make_shared<Body>();
        root->id = 1;
        root->kind = BodyKind::Group;
        root->name = "Root";
        ComponentDefinitions definitions;
        Id id = 1;
        std::size_t total = 0;
        auto add = [&](ComponentParameters record) {
            auto definition = std::make_shared<ComponentDefinition>();
            definition->id = id;
            definition->root = 1;
            definition->nextMemberId = 2;
            definition->members[1] = root;
            definition->parameters = std::make_shared<const ComponentParameters>(std::move(record));
            total += componentParametersBytes(definition->parameters);
            definitions[id++] = definition;
        };
        auto bytesOf = [](const ComponentParameters &record) {
            return componentParametersBytes(std::make_shared<const ComponentParameters>(record));
        };
        auto param = [&](std::size_t index, std::size_t label) {
            auto parameter = numberParameter("f" + std::to_string(index), "1");
            parameter.label = std::string(label, 'L');
            return parameter;
        };
        constexpr auto limit = ComponentParameterLimits::recordBytes;
        while (limit - total > 40000) {
            ComponentParameters record;
            for (std::size_t i = 0; i < 64; ++i)
                record.parameters.push_back(param(i, 128));
            add(std::move(record));
        }
        // The last record: n parameters whose labels (1–128 bytes) absorb the rest exactly.
        const auto remaining = limit - total;
        ComponentParameters last;
        for (std::size_t n = 1; n <= ComponentParameterLimits::parameters; ++n) {
            ComponentParameters candidate;
            for (std::size_t i = 0; i < n; ++i)
                candidate.parameters.push_back(param(i, 1));
            const auto low = bytesOf(candidate);
            if (remaining < low || remaining > low + 127 * n)
                continue;
            auto extra = remaining - low;
            for (auto &parameter : candidate.parameters) {
                const auto grow = std::min<std::size_t>(extra, 127);
                parameter.label += std::string(grow, 'L');
                extra -= grow;
            }
            last = std::move(candidate);
            break;
        }
        add(std::move(last));
        check(total == limit, "The record fixture totals exactly 4 MiB (got " + std::to_string(total) +
                                  ")");
        validateComponentParameterRecords(definitions, nullptr, 1);
        auto grown = std::make_shared<ComponentDefinition>(*definitions.at(1));
        auto record = *grown->parameters;
        record.parameters[0].label.pop_back(); // 127 bytes, then one more parameter byte:
        record.parameters[0].name += "x";      // the same record, one byte larger
        grown->parameters = std::make_shared<const ComponentParameters>(record);
        check(componentParametersBytes(grown->parameters) ==
                  componentParametersBytes(definitions.at(1)->parameters),
              "The resized record keeps its size");
        record.parameters[0].label += "L";
        grown->parameters = std::make_shared<const ComponentParameters>(record);
        definitions[1] = grown;
        expectCode([&] { validateComponentParameterRecords(definitions, nullptr, 1); },
                   "LIMIT_EXCEEDED", "parameter records over 4 MiB");
    }
    // One definition evaluation: the budget is checked before every node visit.
    {
        auto t = makeTable();
        const auto compiled =
            compileComponentParameters(t.doc.definitions().at(t.definition), t.doc.definitions());
        const auto visits = evaluateComponentParameters(*compiled, {}).visits;
        check(evaluateComponentParameters(*compiled, {}, ComponentParameterLimits::documentVisits,
                                          visits)
                      .visits == visits,
              "An evaluation within its budget succeeds");
        expectCode([&] {
            evaluateComponentParameters(*compiled, {}, ComponentParameterLimits::documentVisits,
                                        visits - 1);
        },
                   "LIMIT_EXCEEDED", "an evaluation one visit over its budget");
        const auto error =
            expectCode([&] { evaluateComponentParameters(*compiled, {}, visits - 1); },
                       "LIMIT_EXCEEDED", "an evaluation over the document's remaining budget");
        check(std::string(error.what()).find("document") != std::string::npos,
              "The document budget names itself");
    }
    std::cout << "definition limits: ok\n";
}
// Parameter records and overrides count in history and snapshot accounting.
void accounting() {
    auto t = makeTable();
    auto &doc = t.doc;
    const auto history = doc.historyBytes();
    const auto snapshot = doc.readSnapshotBytes();
    const auto before = componentParametersBytes(doc.definitions().at(t.definition)->parameters);
    auto big = spec("note", std::string(128, 'N'), ParameterType::Number,
                    ComponentParameterMode::Input, sized(1000));
    defineComponentParameter(doc, t.definition, big);
    const auto after = componentParametersBytes(doc.definitions().at(t.definition)->parameters);
    check(after > before + 1100, "The record grows by the new parameter");
    check(doc.historyBytes() - history >= before + after,
          "The history entry charges both parameter records");
    check(doc.readSnapshotBytes() - snapshot >= after - before,
          "Snapshot accounting includes the parameter record");
    const auto overrideHistory = doc.historyBytes();
    overrideComponentParameters(doc, t.placement, {{"width", metres(1.2)}});
    check(doc.historyBytes() - overrideHistory >=
              componentOverridesBytes(doc.instances().at(t.placement)->overrides),
          "The history entry charges the overrides");
    std::cout << "accounting: ok\n";
}
// ADR 0165 §7 "Budget": evaluating the worst accepted definition (128 parameters, 256
// drivers, every formula 256 nodes) stays under ten times the value R093.e measured on
// its reference machine, per build (the sanitized build runs several times slower).
#if defined(__SANITIZE_ADDRESS__)
constexpr double evaluationCeilingMs = 1050; // measured at most 104.2 ms
#else
constexpr double evaluationCeilingMs = 340; // measured at most 34.0 ms
#endif
void evaluationBudget() {
    auto p = makePanel();
    ComponentParameters record;
    record.parameters.push_back(numberParameter("x", "1", ComponentParameterMode::Input));
    for (std::size_t i = 1; i < ComponentParameterLimits::parameters; ++i)
        record.parameters.push_back(numberParameter("q" + std::to_string(i), chain(128)));
    // "-x + ... + x > 0": 127 names, 126 additions, a negation, a literal and a comparison.
    const auto visible = canonicalParameterExpression(parseParameterExpression(chain(127) + " > 0"));
    for (std::size_t i = 1; i <= ComponentParameterLimits::drivers; ++i) {
        ComponentDriver driver;
        driver.id = i;
        driver.kind = ComponentDriverKind::Visibility;
        driver.member = p.member;
        driver.expression = visible;
        record.drivers.push_back(driver);
    }
    auto definitions = p.doc.definitions();
    definitions[p.definition] = withRecord(definitions.at(p.definition), record);
    validateComponentParameterRecords(definitions, nullptr, ComponentParameterLimits::drivers + 1);
    const auto compiled = compileComponentParameters(definitions.at(p.definition), definitions);
    const auto first = evaluateComponentParameters(*compiled, {});
    constexpr std::size_t nodes = 256;
    check(first.visits == 1 + (ComponentParameterLimits::parameters - 1) * nodes +
                              ComponentParameterLimits::drivers * nodes,
          "The worst definition visits every node of every formula once (got " +
              std::to_string(first.visits) + ")");
    check(std::get<bool>(first.drivers.back()), "Its visibility formulas are true");
    // Timed at the defaults, where the input's own default formula is visited too.
    constexpr int runs = 20;
    double best = 1e9;
    for (int run = 0; run < runs; ++run) {
        const auto started = std::chrono::steady_clock::now();
        const auto evaluated = evaluateComponentParameters(*compiled, {});
        best = std::min(best, std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count());
        check(evaluated.visits == first.visits, "Each evaluation visits the same nodes");
    }
    std::cout << "evaluation budget: " << first.visits << " visits, best of " << runs << " "
              << best << " ms; ceiling " << evaluationCeilingMs << " ms\n";
    check(best <= evaluationCeilingMs, "The worst definition evaluates within the ceiling");
}
} // namespace
int main(int argc, char **argv) {
    const std::string argument = argc > 1 ? argv[1] : "";
    const bool print = argument == "--print-golden";
    const std::vector<std::pair<std::string, std::function<void()>>> suites{
        {"digests", digests},
        {"table", tableWidth},
        {"shelves", shelvesRepeat},
        {"determinism", [&] { determinism(print); }},
        {"rebaseline", rebaseline},
        {"sharing", sharing},
        {"nested", nested},
        {"hosted", hosted},
        {"fail-closed", failClosed},
        {"one-placement", onePlacementFails},
        {"cycles", cycles},
        {"locks", locks},
        {"unique", uniqueReplacePromote},
        {"template", templateEdits},
        {"paint", paintKeepsDriversBound},
        {"expression-limits", expressionLimits},
        {"record-limits", recordLimits},
        {"accounting", accounting},
        {"definition-limits", definitionLimits},
        {"document-limits", documentLimits},
        {"evaluation-budget", evaluationBudget},
    };
    try {
        for (const auto &[name, suite] : suites) {
            if (!argument.empty() && !print && argument != name)
                continue;
            // The full-validation oracle cross-checks every edit and undo; the large
            // limit fixtures run without it to stay short under sanitizers.
            Document::setFullValidationOracle(name != "definition-limits" &&
                                              name != "document-limits" &&
                                              name != "evaluation-budget");
            suite();
        }
        std::cout << checks << " component parameter checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
