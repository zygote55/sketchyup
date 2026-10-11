// R082.gg document limits: unique geometry, placement records and materialized
// totals, with D06 diagnostics that name the limit, both values and the entity.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "sanitizer_mode.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
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
    check(rejection([] {
              checkLimits({0, 0, 1234567}, LimitClass::UniqueGeometry, "Test", {});
          }) == "Test exceeds document unique geometry limits: vertices would be 1,234,567 of "
                "100,000",
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
    // A 2,000-vertex definition placed ten times plus 98 loose faces meets the
    // unique vertex limit exactly while 118,000 vertices are materialized.
    // Loose faces first: every later edit revalidates the definitions.
    Document doc;
    for (int i = 0; i < 98; ++i)
        ring(doc, i);
    const auto component = createComponent(doc, prism(doc, 1000), "Large");
    for (int i = 1; i < 10; ++i)
        placeComponent(doc, component.definition, Transform::translation({3.0 * i, 0, 0}));
    check(doc.materializedTotals().vertices == 118000,
          "Unique vertices at the limit while materialized totals exceed 100,000");
    const auto next = doc.nextId();
    check(rejection([&] { ring(doc, 98); }) ==
              "Document edit exceeds document unique geometry limits: vertices would be "
              "101,000 of 100,000 (body #" +
                  std::to_string(next) + ")",
          "An oversized unique-geometry document is rejected with the bounded diagnostic");
    // A unique copy of the definition is new unique geometry.
    const auto copied = rejection([&] { makeComponentUnique(doc, component.instance); });
    check(copied.starts_with("Document edit exceeds document unique geometry limits: vertices "
                             "would be 102,000 of 100,000 (definition '"),
          "A copied definition counts as new unique geometry: " + copied);
    check(doc.materializedTotals().vertices == 118000, "Rejected edits publish nothing");
    // Definitions together are bounded by the unique limits before any placement
    // is resolved: copying a 52,000-vertex definition would hold 104,000.
    Document assembly;
    std::set<Id> parts;
    for (int i = 0; i < 52; ++i)
        parts.insert(ring(assembly, i));
    const auto big = createComponent(assembly, createGroup(assembly, parts, "Tower"), "Tower");
    const auto second = placeComponent(assembly, big.definition, Transform::translation({5, 0, 0}));
    const auto copiedDefinition = rejection([&] { makeComponentUnique(assembly, second.instance); });
    check(copiedDefinition.starts_with("Component definition exceeds document unique geometry "
                                       "limits: vertices would be 104,000 of 100,000 (definition '"),
          "Definitions together are bounded by the unique limits: " + copiedDefinition);
}
} // namespace
int main() {
    try {
        diagnostics();
        placementCount();
        uniqueGeometry();
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
