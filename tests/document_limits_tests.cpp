// R082.gg document limits: unique geometry, placement records and materialized
// totals, with D06 diagnostics that name the limit, both values and the entity.
// R110 (1.7.0) raised the unique limits to 300,000 vertices, faces and wires and 900,000 edges.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/terrain.hpp"
#include "sanitizer_mode.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
Id prism(Document &doc, int sides) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({std::cos(angle), std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    return body;
}
// A flat 1,000-vertex face: unique-vertex ballast without extrusion cost.
Id ring(Document &doc, int level) {
    std::vector<Vec3> loop;
    for (int i = 0; i < 1000; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 1000;
        loop.push_back({std::cos(angle), std::sin(angle), 2.0 * level});
    }
    return doc.addFace({loop});
}
void diagnostics() {
    // Locale-independent grouping and the fixed quantity order.
    LimitCounts counts{20001, 40001};
    check(exceededLimit(counts, LimitClass::Placement) == "placements",
          "Placements are reported before bodies");
    check(exceededLimit(counts, LimitClass::UniqueGeometry) == "bodies",
          "Unique geometry ignores placement records");
    check(rejection([&] { checkLimits(counts, LimitClass::Placement, "Test", "body #7"); }) ==
              "Test exceeds document placement limits: placements would be 20,001 of 20,000 "
              "(body #7)",
          "Placement diagnostic format");
    check(rejection([] { checkLimits({0, 0, 1234567}, LimitClass::UniqueGeometry, "Test", {}); }) ==
              "Test exceeds document unique geometry limits: vertices would be 1,234,567 of "
              "300,000",
          "Unique diagnostic format without context");
    check(rejection([] { checkLimits({20000, 40000}, LimitClass::Placement, "Test", {}); })
              .empty(),
          "Values at the limit are accepted");

    // Names: control characters replaced, UTF-8 sequences never split, 64 bytes.
    check(quotedLimitName("Chair\n\x01") == "'Chair?" "?'", "Control characters are replaced");
    std::string long_name;
    for (int i = 0; i < 40; ++i)
        long_name += "\xC3\xA9"; // é, two bytes each
    const auto quoted = quotedLimitName(long_name);
    check(quoted.size() <= limitNameBytes + 2, "Quoted names are bounded");
    check(quoted.substr(quoted.size() - 4) == "\xE2\x80\xA6'", "Truncated names end with an ellipsis");
    const auto body = quoted.substr(1, quoted.size() - 5);
    check(body.size() % 2 == 0 && body.size() == 60, "Truncation keeps whole UTF-8 sequences");

    // Whole messages stay bounded even with maximal values and a long name.
    const auto huge = std::string(500, 'x');
    const auto message = rejection([&] {
        checkLimits({SIZE_MAX, SIZE_MAX}, LimitClass::Placement, "Component placement",
                    "placing " + definitionLimitContext(huge, SIZE_MAX) + " into group #" +
                        std::to_string(SIZE_MAX));
    });
    check(!message.empty() && message.size() <= limitMessageBytes,
          "Diagnostics never exceed " + std::to_string(limitMessageBytes) + " bytes");
    check(message.find(std::string(65, 'x')) == std::string::npos,
          "Definition names are truncated in diagnostics");
}
void uniqueBoundaries() {
    // R110: every unique quantity is accepted exactly at its limit, and one more is refused with
    // the D06 message naming the new maximum. Placements are not part of the unique class.
    const LimitCounts atLimit{0, 10000, 300000, 300000, 300000, 900000, 10000, 10000};
    check(exceededLimit(atLimit, LimitClass::UniqueGeometry).empty(),
          "Unique values at the limits are accepted");
    check(rejection([&] { checkLimits(atLimit, LimitClass::UniqueGeometry, "Test", {}); }).empty(),
          "Unique values at the limits pass checkLimits");
    const std::vector<std::pair<LimitCounts, std::string>> over{
        {{0, 0, 300001}, "vertices would be 300,001 of 300,000"},
        {{0, 0, 0, 300001}, "faces would be 300,001 of 300,000"},
        {{0, 0, 0, 0, 300001}, "wires would be 300,001 of 300,000"},
        {{0, 0, 0, 0, 0, 900001}, "edges would be 900,001 of 900,000"},
        {{0, 10001}, "bodies would be 10,001 of 10,000"},
        {{0, 0, 0, 0, 0, 0, 10001}, "curves would be 10,001 of 10,000"},
        {{0, 0, 0, 0, 0, 0, 0, 10001}, "guides would be 10,001 of 10,000"},
    };
    for (const auto &[counts, text] : over)
        check(rejection([&] { checkLimits(counts, LimitClass::UniqueGeometry, "Test", {}); }) ==
                  "Test exceeds document unique geometry limits: " + text,
              "One more than the unique limit is refused: " + text);
    // Each unique value stays at or below its materialized value (checked at compile time too).
    check(DocumentLimits::unique.vertices == 300000 && DocumentLimits::unique.edges == 900000,
          "The unique values are the R110 values");
    check(DocumentLimits::materialized.vertices == 600000 &&
              DocumentLimits::materialized.edges == 900000 &&
              DocumentLimits::materialized.placements == 20000,
          "The materialized values and the placement limit did not change");
}
void publicPlacements(size_t count) {
    // The R082.a/ADR 0141 fixture shape: a 26-sided prism placed 10,000 times.
    // It was rejected at placement 1,924 before R082.gg.
    Document doc;
    const auto component = createComponent(doc, prism(doc, 26), "26-sided benchmark prism");
    const int columns = 100;
    for (size_t i = 1; i < count; ++i)
        placeComponent(doc, component.definition,
                       Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
    const auto &totals = doc.materializedTotals();
    check(doc.instances().size() == count && totals.records == 2 * count &&
              totals.vertices == 52 * count && totals.faces == 28 * count &&
              totals.edges == 78 * count,
          "The public path places " + std::to_string(count) + " instances (" +
              std::to_string(totals.vertices) + " vertices)");
    size_t triangles{};
    for (const auto &[id, body] : doc.bodies())
        triangles += body->surface.triangles().size();
    check(triangles == 100 * count, "The placed fixture has 100 triangles per instance");
}
void placementCount() {
    // A definition nesting 99 triangle placements expands to 100 placements and
    // 199 records. 200 placements of it meet the placement limit exactly.
    static_assert(DocumentLimits::materialized.placements == 20000);
    Document doc;
    const auto triangle =
        createComponent(doc, doc.addFace({{{0, 0, 0}, {.5, 0, 0}, {0, .5, 0}}}), "Triangle");
    std::set<Id> members{triangle.instance};
    for (int i = 1; i < 99; ++i)
        members.insert(placeComponent(doc, triangle.definition,
                                      Transform::translation({1.0 * i, 0, 0}))
                           .instance);
    const auto row = createComponent(doc, createGroup(doc, members, "Row"), "Row");
    check(doc.instances().size() == 100, "Nested row expands to 100 placements");
    for (int i = 1; i < 200; ++i)
        placeComponent(doc, row.definition, Transform::translation({0, 1.0 * i, 0}));
    check(doc.instances().size() == DocumentLimits::materialized.placements,
          "Placements up to the placement limit succeed");
    check(rejection([&] { placeComponent(doc, triangle.definition, {}, 0); }) ==
              "Component placement exceeds document placement limits: placements would be "
              "20,001 of 20,000 (placing definition 'Triangle' #" +
                  std::to_string(triangle.definition) + " into the model)",
          "An over-limit placement count is rejected with the bounded diagnostic");
    const auto group = doc.nextId();
    auto empty = std::make_shared<Body>();
    empty->id = group;
    empty->kind = BodyKind::Group;
    doc.apply({"Group", {{group, nullptr, empty}}}, doc.revision());
    check(rejection([&] { placeComponent(doc, row.definition, {}, group); }) ==
              "Component placement exceeds document placement limits: placements would be "
              "20,100 of 20,000 (placing definition 'Row' #" +
                  std::to_string(row.definition) + " into group #" + std::to_string(group) +
                  ")",
          "Nested placements count toward the placement limit");
}
void uniqueGeometry() {
    // Unique geometry counts loose records and each definition's members once.
    // A 2,000-vertex definition placed ten times plus 298 loose faces of 1,000 vertices meets
    // the unique vertex limit exactly (300,000) while 318,000 vertices are materialized.
    // Loose faces first: every later edit revalidates the definitions.
    Document doc;
    for (int i = 0; i < 298; ++i)
        ring(doc, i);
    const auto component = createComponent(doc, prism(doc, 1000), "Large");
    for (int i = 1; i < 10; ++i)
        placeComponent(doc, component.definition, Transform::translation({3.0 * i, 0, 0}));
    check(doc.materializedTotals().vertices == 318000,
          "Unique vertices at the limit while materialized totals exceed 300,000");
    const auto next = doc.nextId();
    check(rejection([&] { ring(doc, 298); }) ==
              "Document edit exceeds document unique geometry limits: vertices would be "
              "301,000 of 300,000 (body #" +
                  std::to_string(next) + ")",
          "An oversized unique-geometry document is rejected with the bounded diagnostic");
    // A unique copy of the definition is new unique geometry.
    const auto copied = rejection([&] { makeComponentUnique(doc, component.instance); });
    check(copied.starts_with("Document edit exceeds document unique geometry limits: vertices "
                             "would be 302,000 of 300,000 (definition '"),
          "A copied definition counts as new unique geometry: " + copied);
    check(doc.materializedTotals().vertices == 318000, "Rejected edits publish nothing");

    // Definitions together are bounded by the unique limits before any placement is resolved:
    // copying a 151,000-vertex definition would hold 302,000.
    Document assembly;
    std::set<Id> parts;
    for (int i = 0; i < 151; ++i)
        parts.insert(ring(assembly, i));
    const auto big = createComponent(assembly, createGroup(assembly, parts, "Tower"), "Tower");
    const auto second = placeComponent(assembly, big.definition, Transform::translation({5, 0, 0}));
    const auto copiedDefinition = rejection([&] { makeComponentUnique(assembly, second.instance); });
    check(
        copiedDefinition.starts_with("Component definition exceeds document unique geometry "
                                     "limits: vertices would be 302,000 of 300,000 (definition '"),
        "Definitions together are bounded by the unique limits: " + copiedDefinition);
}
// The members of a heavy component: `rings` flat 1,000-vertex faces. Build every member before
// any component exists, because each later edit revalidates the definitions already present.
std::set<Id> ringParts(Document &doc, int rings, int firstLevel = 0) {
    std::set<Id> parts;
    for (int i = 0; i < rings; ++i)
        parts.insert(ring(doc, firstLevel + i));
    return parts;
}
ComponentResult componentOf(Document &doc, const std::set<Id> &parts, const std::string &name) {
    return createComponent(doc, createGroup(doc, parts, name), name);
}
Id firstMember(const Document &doc, Id definition) {
    const auto &record = *doc.definitions().at(definition);
    for (const auto &[member, body] : record.members)
        if (member != record.root)
            return member;
    throw std::runtime_error("Definition has no member");
}
// D-075: the draft of a component edit holds the edited definition's members once. The
// stored record that the edit replaces is not counted again, so a 160,000-vertex definition
// (its two copies would hold 320,000, over the limit) can be edited, while the real document
// still refuses an edit that exceeds the limit.
void draftCountsEditedDefinitionOnce() {
    Document doc;
    const auto big = componentOf(doc, ringParts(doc, 160), "Big");
    check(doc.materializedTotals().vertices == 160000, "Single-instance definition is 160,000");
    const auto member = firstMember(doc, big.definition);
    editComponentDefinition(doc, big.definition, [&](Document &draft) {
        draft.move(member, {1, 0, 0});
        return ChangeReport{};
    });
    check(doc.materializedTotals().vertices == 160000,
          "Moving one member of a 160,000-vertex definition succeeds");
    doc.undo();
    check(doc.materializedTotals().vertices == 160000, "The move undoes to the same geometry");

    // The document-wide check counts the real document: 100,000 + 100,000 of definitions and
    // 96,000 loose vertices make 296,000. Growing 'Large' by 5,000 vertices in the draft makes
    // 301,000, which is refused at publish. The stored records alone (200,000 before, 205,000
    // after) stay within the limit, so the document-wide count is what refuses the edit. Few
    // rings are added inside the draft: each nested command revalidates the whole draft.
    Document over;
    const auto largeParts = ringParts(over, 100);
    const auto smallParts = ringParts(over, 100, 100);
    for (int i = 0; i < 96; ++i)
        ring(over, 200 + i);
    const auto large = componentOf(over, largeParts, "Large");
    componentOf(over, smallParts, "Small");
    check(over.materializedTotals().vertices == 296000,
          "Fixture: 296,000 unique vertices before the edit");
    const auto refused = rejection([&] {
        editComponentDefinition(over, large.definition, [&](Document &draft) {
            for (int i = 0; i < 5; ++i)
                ring(draft, 300 + i);
            return ChangeReport{};
        });
    });
    check(refused == "Document edit exceeds document unique geometry limits: vertices would be "
                     "301,000 of 300,000 (definition 'Large' #" +
                         std::to_string(large.definition) + ")",
          "The real document still refuses a component edit over the limit: " + refused);
    check(over.materializedTotals().vertices == 296000,
          "A refused component edit publishes nothing");

    // A definition that exceeds the limit alone is not built here: one edit that creates about
    // 300,000 vertices of members is refused by the 64 MiB undo history budget (R110 finding).
}

// R110: Explode terrain joins the terrain's triangles to the modelling faces against the unique
// face limit. A 249,218-triangle terrain (the terrain limit's own size) and 50,782 loose modelling
// faces make exactly 300,000: the explode goes through, and undo restores the terrain. One more
// face is refused with the numbers, and the refusal changes nothing. (Building the terrain and
// the faces takes about half a minute, which is why this runs in the slow core group.)
TerrainMesh terrainGrid(int nx, int ny) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back(
                {double(i), double(j), .25 * std::sin(i * .3) + .15 * std::cos(j * .2)});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}
// A prism of `sides` sides centred at (x, 400): loose modelling geometry, sides + 2 faces.
void looseFacesPrism(Document &doc, int sides, double x) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({x + std::cos(angle), 400 + std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
}
void terrainExplodeAtLimit() {
    Document doc;
    const auto mesh = terrainGrid(353, 353); // 249,218 triangles
    TerrainRecord record;
    record.generator = "test/terrain-v1";
    record.parameters.maxEdgeLength = 3;
    const auto terrain = createTerrain(doc, mesh, record).terrain;
    check(uniqueGeometryCounts(doc.materializedTotals(), {}, doc.definitions(),
                               &doc.componentVariants())
                  .faces == 0,
          "A 249,218-triangle terrain holds no modelling faces");
    // Fifty prisms of 1,000 sides (1,002 faces each) and one of 680 sides (682 faces): 50,782.
    for (int i = 0; i < 50; ++i)
        looseFacesPrism(doc, 1000, 3.0 * i);
    looseFacesPrism(doc, 680, 150.0);
    check(uniqueGeometryCounts(doc.materializedTotals(), {}, doc.definitions(),
                               &doc.componentVariants())
                  .faces == 50782,
          "The loose prisms hold 50,782 modelling faces");
    explodeTerrain(doc, terrain);
    check(doc.history().entries.back().label == "Explode terrain" && !isTerrain(doc, terrain),
          "Exploding at exactly 300,000 faces succeeds");
    doc.undo();
    check(isTerrain(doc, terrain), "Undo restores the terrain at the limit");
    doc.addFace({{{500, 400, 0}, {501, 400, 0}, {500, 401, 0}}});
    const auto revision = doc.revision();
    std::string refusal;
    try {
        explodeTerrain(doc, terrain);
    } catch (const std::exception &error) {
        refusal = error.what();
    }
    check(refusal == "Explode terrain would make 300,001 faces, over the 300,000-face modelling "
                     "limit (the terrain has 249,218 triangles)",
          "Explode over the unique face limit names the numbers: " + refusal);
    check(doc.revision() == revision && isTerrain(doc, terrain), "The refusal changes nothing");
}
} // namespace

int main() {
    try {
        diagnostics();
        uniqueBoundaries();
        placementCount();
        uniqueGeometry();
        draftCountsEditedDefinitionOnce();
        terrainExplodeAtLimit();
        // The full 10,000-placement public path runs in unsanitized builds only;
        // the serial sanitized CI job runs the same path at 1,000 placements.
        const size_t placements = test::sanitized ? 1000 : 10000;
        publicPlacements(placements);
        std::cout << "Document limits: unique geometry, placement records, materialized "
                     "totals and bounded diagnostics passed ("
                  << (test::sanitized ? "sanitized mode: " : "full mode: ") << placements
                  << " public placements)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
